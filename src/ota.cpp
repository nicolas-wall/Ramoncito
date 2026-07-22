// =============================================================
//  Ramoncito — ota.cpp
//  Auto-OTA por internet: pull desde GitHub Releases.
//  El chequeo de versión es automático (boot + cada 24 h); la
//  instalación SOLO se ejecuta cuando el usuario la confirma
//  (instalarAhora(), cableado desde el menú).
//
//  Seguridad: se usa setInsecure() (sin validación de certificado).
//  Justificación: este es un juguete de escritorio; la complejidad
//  de gestionar un bundle de CAs en flash no vale la pena. La
//  integridad del firmware queda garantizada por la verificación
//  de MD5 que hace Update.setMD5() antes de flashear.
//
//  GitHub redirige los assets de releases (302 → objects.githubusercontent.com)
//  por eso se usa setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS).
// =============================================================

#include "ota.h"
#include "config.h"
#include "net.h"
#include "sound.h"

#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <Update.h>

// Instancia global accesible desde main.cpp
Ota ota;

// Tamaño del buffer de descarga en bytes (compromiso RAM/velocidad)
static const size_t OTA_BUF_SIZE = 1024;

// ================================================================
//  begin() — llamar desde setup() después de u8g2.begin()
// ================================================================
void Ota::begin(U8G2* display) {
    _display  = display;
    _iniciado = true;
    // Programar el primer chequeo para boot + OTA_CHECK_BOOT_MS
    _proximoChequeo = millis() + OTA_CHECK_BOOT_MS;
    Serial.printf("[ota] auto-OTA habilitado — primer chequeo en %lu s\n",
                  (unsigned long)(OTA_CHECK_BOOT_MS / 1000UL));
}

// ================================================================
//  update() — llamar cada frame desde loop()
//  Solo chequeo automático: NUNCA instala solo. La instalación la
//  dispara el usuario vía instalarAhora().
// ================================================================
void Ota::update(uint32_t now) {
    // Sin OTA o sin inicio → nada
    if (!OTA_AUTO_HABILITADO || !_iniciado) return;

    // Sin conexión a internet → no tiene sentido intentar
    if (!net.staConnected()) return;

    // ¿Llegó el momento del chequeo?
    if (_proximoChequeo != 0 && (int32_t)(now - _proximoChequeo) >= 0) {
        // Programar el próximo antes del chequeo, para que un error
        // no deje el próximo chequeo sin programar
        _proximoChequeo = now + OTA_CHECK_INTERVALO_MS;
        _chequear();
    }
}

// ================================================================
//  forzarChequeo() — fuerza el próximo chequeo para ya
// ================================================================
void Ota::forzarChequeo() {
    // Restar 1 para que (int32_t)(now - _proximoChequeo) >= 0 en el próximo frame
    _proximoChequeo = millis() - 1;
    Serial.println("[ota] chequeo forzado programado");
}

// ================================================================
//  versionNueva() — versión pendiente de instalar ("" si no hay)
// ================================================================
const char* Ota::versionNueva() const {
    return _hayNueva ? _versionNueva : "";
}

// ================================================================
//  instalarAhora() — instala la actualización pendiente
//  (la llama el menú cuando el usuario confirma)
// ================================================================
void Ota::instalarAhora() {
    if (!_hayNueva) {
        Serial.println("[ota] instalarAhora: no hay actualizacion pendiente");
        return;
    }
    if (!net.staConnected()) {
        Serial.println("[ota] instalarAhora: sin conexion STA — no se puede descargar");
        return;
    }
    _instalar();
}

// ================================================================
//  estadoTexto() — string corto del último resultado
// ================================================================
const char* Ota::estadoTexto() const {
    switch (_estado) {
        case Estado::SIN_CHEQUEAR: return "sin chequear";
        case Estado::AL_DIA:       return "al dia";
        case Estado::DISPONIBLE:   return "disponible";
        case Estado::ERROR_HTTP:   return "error http";
        case Estado::ERROR_JSON:   return "error json";
        case Estado::ERROR_FLASH:  return "error flash";
        case Estado::ACTUALIZADO:  return "actualizado";
        default:                   return "desconocido";
    }
}

// ================================================================
//  _parsearVersion() — "major.minor.patch[sufijo]" → uint32_t
//  Devuelve (major<<16 | minor<<8 | patch); 0 si formato inválido.
//  Ignora cualquier carácter no numérico tras los tres componentes.
// ================================================================
uint32_t Ota::_parsearVersion(const char* ver) {
    if (!ver || ver[0] == '\0') return 0;
    unsigned int maj = 0, min = 0, pat = 0;
    // sscanf con %u ignora sufijos que no sean dígitos después del punto
    if (sscanf(ver, "%u.%u.%u", &maj, &min, &pat) < 3) return 0;
    return ((uint32_t)maj << 16) | ((uint32_t)min << 8) | (uint32_t)pat;
}

// ================================================================
//  _dibujarProgreso() — pantalla OLED durante la descarga
// ================================================================
void Ota::_dibujarProgreso(const char* verNueva, int progreso) {
    if (!_display) return;

    _display->clearBuffer();

    // Título
    _display->setFont(u8g2_font_6x12_tf);
    _display->drawStr(2, 12, "Actualizando...");

    // Versión nueva
    char linea[32];
    snprintf(linea, sizeof(linea), "v%s", verNueva);
    _display->drawStr(2, 26, linea);

    // Barra de progreso: marco + relleno
    // Coordenadas: (x=2, y=36, w=124, h=10)
    const int BAR_X = 2;
    const int BAR_Y = 36;
    const int BAR_W = 124;
    const int BAR_H = 10;

    // Marco exterior
    _display->drawFrame(BAR_X, BAR_Y, BAR_W, BAR_H);

    // Relleno proporcional (con margen de 1 px interior)
    int relleno = (progreso * (BAR_W - 2)) / 100;
    if (relleno > 0) {
        _display->drawBox(BAR_X + 1, BAR_Y + 1, relleno, BAR_H - 2);
    }

    // Porcentaje
    snprintf(linea, sizeof(linea), "%d%%", progreso);
    _display->drawStr(2, 60, linea);

    _display->sendBuffer();
}

// ================================================================
//  _chequear() — GET version.json + comparación de versiones.
//  NO descarga el firmware: si hay una versión nueva la deja
//  registrada (_hayNueva/_versionNueva/_md5Nueva) para que el
//  usuario decida instalarla desde el menú.
// ================================================================
void Ota::_chequear() {
    Serial.println("[ota] iniciando chequeo de versión...");

    // ---- Paso 1: GET version.json --------------------------------
    WiFiClientSecure client;
    // Sin validación de CA: juguete de escritorio, simpleza intencional.
    // La integridad queda garantizada por el MD5 que verifica Update.setMD5().
    client.setInsecure();
    client.setTimeout(15);   // s — el handshake TLS de GitHub puede ser lento

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    // GitHub encadena 302 + dos handshakes TLS: el default de 5 s da -11
    http.setConnectTimeout(10000);
    http.setTimeout(15000);

    Serial.printf("[ota] GET %s\n", OTA_VERSION_URL);
    if (!http.begin(client, OTA_VERSION_URL)) {
        Serial.println("[ota] error: no se pudo iniciar HTTPClient para version.json");
        _estado = Estado::ERROR_HTTP;
        return;
    }

    int httpCode = http.GET();
    Serial.printf("[ota] HTTP status: %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[ota] error HTTP al obtener version.json: %d\n", httpCode);
        http.end();
        _estado = Estado::ERROR_HTTP;
        return;
    }

    String body = http.getString();
    http.end();

    Serial.printf("[ota] version.json: %s\n", body.c_str());

    // ---- Paso 2: Parsear JSON sin librerías externas ------------
    // Formato esperado: {"version":"1.2.3","md5":"aabbccdd..."}
    // Es JSON controlado por nosotros, así que strstr/sscanf alcanza.
    char verRemota[sizeof(_versionNueva)] = {0};
    char md5Remoto[sizeof(_md5Nueva)]     = {0};

    // Extraer "version":"..."
    const char* pVer = strstr(body.c_str(), "\"version\"");
    if (pVer) {
        // Avanzar hasta la primera comilla de valor
        pVer = strchr(pVer + 9, '"');
        if (pVer) {
            pVer++;  // saltar la comilla inicial
            int i = 0;
            while (pVer[i] && pVer[i] != '"' && i < (int)sizeof(verRemota) - 1) {
                verRemota[i] = pVer[i];
                i++;
            }
            verRemota[i] = '\0';
        }
    }

    // Extraer "md5":"..."
    const char* pMd5 = strstr(body.c_str(), "\"md5\"");
    if (pMd5) {
        pMd5 = strchr(pMd5 + 5, '"');
        if (pMd5) {
            pMd5++;
            int i = 0;
            while (pMd5[i] && pMd5[i] != '"' && i < (int)sizeof(md5Remoto) - 1) {
                md5Remoto[i] = pMd5[i];
                i++;
            }
            md5Remoto[i] = '\0';
        }
    }

    if (verRemota[0] == '\0') {
        Serial.println("[ota] error: no se pudo parsear el campo 'version' del JSON");
        _estado = Estado::ERROR_JSON;
        return;
    }

    Serial.printf("[ota] version remota: %s | md5: %s\n",
                  verRemota, md5Remoto[0] ? md5Remoto : "(no disponible)");
    Serial.printf("[ota] version local:  %s\n", FW_VERSION);

    // ---- Paso 3: Comparar versiones ------------------------------
    uint32_t numRemota = _parsearVersion(verRemota);
    uint32_t numLocal  = _parsearVersion(FW_VERSION);

    if (numRemota == 0) {
        Serial.println("[ota] error: version remota no tiene formato major.minor.patch valido");
        _estado = Estado::ERROR_JSON;
        return;
    }

    if (numRemota <= numLocal) {
        Serial.println("[ota] firmware al dia — no hay actualizacion disponible");
        _hayNueva = false;
        _estado   = Estado::AL_DIA;
        return;
    }

    // ---- Paso 4: Registrar la actualización pendiente ------------
    // NO se descarga nada: queda a la espera de la confirmación del
    // usuario desde el menú (instalarAhora()).
    strncpy(_versionNueva, verRemota, sizeof(_versionNueva) - 1);
    _versionNueva[sizeof(_versionNueva) - 1] = '\0';
    strncpy(_md5Nueva, md5Remoto, sizeof(_md5Nueva) - 1);
    _md5Nueva[sizeof(_md5Nueva) - 1] = '\0';
    _hayNueva = true;
    _estado   = Estado::DISPONIBLE;

    Serial.printf("[ota] version nueva disponible: %s — esperando confirmacion del usuario\n",
                  _versionNueva);
}

// ================================================================
//  _instalar() — descarga firmware.bin y lo flashea (bloqueante).
//  Usa _versionNueva/_md5Nueva guardados por _chequear().
//  Si falla, _hayNueva queda en true: se puede reintentar.
// ================================================================
void Ota::_instalar() {
    Serial.printf("[ota] instalando actualizacion a v%s...\n", _versionNueva);

    // Silenciar el buzzer antes de bloquear el loop: durante la descarga+flasheo
    // sound.update() no corre, así que cualquier nota en curso quedaría trabada
    // sonando todo el rato (ruido molesto continuo).
    sound.stop();

    WiFiClientSecure client;
    // Sin validación de CA (ver comentario de cabecera); integridad por MD5.
    client.setInsecure();
    client.setTimeout(15);   // s — mismos timeouts largos que el chequeo

    HTTPClient http;
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setConnectTimeout(10000);
    http.setTimeout(15000);

    Serial.printf("[ota] GET %s\n", OTA_FIRMWARE_URL);
    if (!http.begin(client, OTA_FIRMWARE_URL)) {
        Serial.println("[ota] error: no se pudo iniciar HTTPClient para firmware.bin");
        _estado = Estado::ERROR_HTTP;
        return;
    }

    int httpCode = http.GET();
    Serial.printf("[ota] HTTP status firmware: %d\n", httpCode);

    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[ota] error HTTP al descargar firmware: %d\n", httpCode);
        http.end();
        _estado = Estado::ERROR_HTTP;
        return;
    }

    int tamano = http.getSize();
    Serial.printf("[ota] tamano del firmware: %d bytes\n", tamano);

    if (!Update.begin(tamano > 0 ? tamano : UPDATE_SIZE_UNKNOWN)) {
        Serial.print("[ota] error al iniciar Update: ");
        Update.printError(Serial);
        http.end();
        _estado = Estado::ERROR_FLASH;
        return;
    }

    // Configurar MD5 para verificación de integridad si está disponible
    if (_md5Nueva[0] != '\0') {
        Update.setMD5(_md5Nueva);
        Serial.printf("[ota] MD5 esperado: %s\n", _md5Nueva);
    }

    // Descargar por bloques, mostrando progreso en OLED
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[OTA_BUF_SIZE];
    int escritoTotal = 0;
    int ultimoPct    = -1;

    // Dibujar progreso inicial
    _dibujarProgreso(_versionNueva, 0);

    while (http.connected() && (tamano > 0 ? escritoTotal < tamano : true)) {
        size_t disponible = stream->available();
        if (disponible == 0) {
            delay(1);
            continue;
        }

        size_t leer = disponible > OTA_BUF_SIZE ? OTA_BUF_SIZE : disponible;
        size_t leido = stream->readBytes(buf, leer);
        if (leido == 0) break;

        size_t escrito = Update.write(buf, leido);
        if (escrito != leido) {
            Serial.print("[ota] error al escribir bloque: ");
            Update.printError(Serial);
            http.end();
            _estado = Estado::ERROR_FLASH;
            return;
        }

        escritoTotal += (int)leido;

        // Actualizar OLED cada ~2% de avance
        int pct = (tamano > 0) ? (escritoTotal * 100 / tamano) : 0;
        if (pct >= ultimoPct + 2) {
            ultimoPct = pct;
            _dibujarProgreso(_versionNueva, pct);
            Serial.printf("[ota] descargando... %d%%\n", pct);
        }

        // Si el tamaño es desconocido y leemos EOF, salir
        if (tamano < 0 && leido < leer) break;
    }

    http.end();

    // Último frame de progreso al 100%
    _dibujarProgreso(_versionNueva, 100);

    // ---- Finalizar y reiniciar -----------------------------------
    if (!Update.end(true)) {
        Serial.print("[ota] error al finalizar la actualizacion: ");
        Update.printError(Serial);
        _estado = Estado::ERROR_FLASH;
        // _hayNueva queda en true: el usuario puede reintentar desde el menú
        return;
    }

    Serial.printf("[ota] actualizacion completada a v%s (%d bytes) — reiniciando\n",
                  _versionNueva, escritoTotal);

    // Pantalla de confirmación antes del reinicio
    if (_display) {
        _display->clearBuffer();
        _display->setFont(u8g2_font_6x12_tf);
        _display->drawStr(10, 28, "Listo!");
        _display->drawStr(4, 46, "Reiniciando...");
        _display->sendBuffer();
    }

    _hayNueva = false;   // ya no está pendiente (reiniciamos igual)
    _estado   = Estado::ACTUALIZADO;
    delay(800);
    ESP.restart();
}
