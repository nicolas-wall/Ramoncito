// =============================================================
//  espToy — net.cpp
//  Implementación del módulo de red.
//
//  Máquina de estados:
//    SIN_CREDENCIALES → PORTAL
//    CONECTANDO       → SINCRONIZANDO_NTP  (WL_CONNECTED antes de timeout)
//    CONECTANDO       → PORTAL             (timeout WIFI_TIMEOUT_MS)
//    SINCRONIZANDO_NTP→ REPOSO             (hora válida o timeout 15 s)
//    REPOSO           → CONECTANDO         (resync diario NTP_RESYNC_MS)
//    PORTAL           → reinicio por ESP.restart() al guardar credenciales
//
//  El portal cautivo responde también a las sondas de Android/iOS para
//  que el teléfono muestre la notificación de "iniciar sesión".
// =============================================================

#include "net.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>

// Instancia global accesible desde main.cpp
Net net;

// Constantes internas
static const uint32_t NTP_TIMEOUT_MS   = 15000;  // 15 s esperando NTP
static const uint8_t  DNS_PORT         = 53;
static const IPAddress AP_IP           (192, 168, 4, 1);
static const IPAddress AP_SUBNET       (255, 255, 255, 0);

// ================================================================
//  begin() — llamar desde setup()
// ================================================================
void Net::begin() {
    _cargarCredenciales();

    if (_ssid.length() > 0) {
        _iniciarConexion();
    } else {
        Serial.println("[net] sin credenciales guardadas → iniciando portal");
        _iniciarPortal();
    }
}

// ================================================================
//  update() — llamar en cada frame/loop, SIN delay()
// ================================================================
void Net::update(uint32_t now) {
    switch (_estado) {

        // ---- CONECTANDO ------------------------------------------
        case Estado::CONECTANDO: {
            wl_status_t st = WiFi.status();
            if (st == WL_CONNECTED) {
                Serial.printf("[net] conectado a '%s' IP: %s\n",
                              _ssid.c_str(),
                              WiFi.localIP().toString().c_str());
                // Configurar zona horaria y disparar NTP
                configTime(TZ_OFFSET_S, 0, NTP_SERVER);
                _tInicioNTP = now;
                _estado = Estado::SINCRONIZANDO_NTP;
            } else if ((now - _tInicioConexion) >= WIFI_TIMEOUT_MS) {
                Serial.printf("[net] timeout de conexión a '%s' → portal\n",
                              _ssid.c_str());
                WiFi.disconnect(true);
                _iniciarPortal();
            }
            break;
        }

        // ---- SINCRONIZANDO_NTP -----------------------------------
        case Estado::SINCRONIZANDO_NTP: {
            time_t ahora = time(nullptr);
            if (ahora > 1600000000UL) {
                // Hora válida obtenida por NTP
                struct tm ti;
                getLocalTime(&ti);
                Serial.printf("[net] NTP ok — hora local: %02d:%02d:%02d %02d/%02d/%04d\n",
                              ti.tm_hour, ti.tm_min, ti.tm_sec,
                              ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
                _tUltimaSync = now;
                // Apagar WiFi para ahorrar energía y reducir ruido en el touch
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                _marcarHoraValida();
                _estado = Estado::REPOSO;
            } else if ((now - _tInicioNTP) >= NTP_TIMEOUT_MS) {
                Serial.println("[net] advertencia: timeout esperando NTP — sin hora confiable");
                WiFi.disconnect(true);
                WiFi.mode(WIFI_OFF);
                _estado = Estado::REPOSO;
            }
            break;
        }

        // ---- REPOSO ----------------------------------------------
        case Estado::REPOSO: {
            // Resync diario: si hay credenciales y pasó el tiempo
            if (_ssid.length() > 0 &&
                _tUltimaSync > 0 &&
                (now - _tUltimaSync) >= NTP_RESYNC_MS) {
                Serial.println("[net] resync NTP diario — reconectando...");
                _iniciarConexion();
            }
            break;
        }

        // ---- PORTAL ----------------------------------------------
        case Estado::PORTAL: {
            if (_dns)    _dns->processNextRequest();
            if (_server) _server->handleClient();
            break;
        }

        // ---- SIN_CREDENCIALES ------------------------------------
        // Este estado solo existe brevemente en begin(); update() no llega aquí
        // ya que begin() transiciona a PORTAL o CONECTANDO antes de volver.
        case Estado::SIN_CREDENCIALES:
            break;
    }
}

// ================================================================
//  timeValid() — hora real confiable (NTP o teléfono)
// ================================================================
bool Net::timeValid() const {
    return _horaValida;
}

// ================================================================
//  isNight() — ventana nocturna según hora local (o forzada)
// ================================================================
bool Net::isNight() const {
    int h = hourNow();
    if (h < 0) return false;  // sin hora → no es de noche (failsafe)
    // Noche si h >= HORA_DORMIR O h < HORA_DESPERTAR
    return (h >= HORA_DORMIR || h < HORA_DESPERTAR);
}

// ================================================================
//  hourNow() — 0-23 o -1 si no hay hora disponible
// ================================================================
int Net::hourNow() const {
    // Hora forzada (modo test/serial)
    if (_horaForzada >= 0) return _horaForzada;
    // Hora real del RTC interno
    if (_horaValida) {
        struct tm ti;
        if (getLocalTime(&ti)) {
            return ti.tm_hour;
        }
    }
    return -1;
}

// ================================================================
//  forceHour() — -1 desactiva el forzado
// ================================================================
void Net::forceHour(int h) {
    _horaForzada = h;
}

// ================================================================
//  portalActive()
// ================================================================
bool Net::portalActive() const {
    return (_estado == Estado::PORTAL);
}

// ================================================================
//  justGotValidTime() — one-shot
// ================================================================
bool Net::justGotValidTime() {
    if (_flagJustGotTime) {
        _flagJustGotTime = false;
        return true;
    }
    return false;
}

// ================================================================
//  startPortal() — forzar el portal manualmente
// ================================================================
void Net::startPortal() {
    if (_estado == Estado::PORTAL) return;  // ya está activo
    // Asegurarse de que el WiFi en modo STA esté desconectado
    WiFi.disconnect(true);
    _iniciarPortal();
}

// ================================================================
//  _cargarCredenciales() — leer ssid/pass de NVS
// ================================================================
void Net::_cargarCredenciales() {
    Preferences prefs;
    prefs.begin("esptoy", true);  // solo lectura
    _ssid = prefs.getString("wifi_ssid", "");
    _pass = prefs.getString("wifi_pass", "");
    prefs.end();

    if (_ssid.length() > 0) {
        Serial.printf("[net] credenciales cargadas para SSID: '%s'\n", _ssid.c_str());
    }
}

// ================================================================
//  _iniciarConexion() — WiFi.begin() en modo STA
// ================================================================
void Net::_iniciarConexion() {
    Serial.printf("[net] conectando a '%s'...\n", _ssid.c_str());
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid.c_str(), _pass.c_str());
    _tInicioConexion = millis();
    _estado = Estado::CONECTANDO;
}

// ================================================================
//  _marcarHoraValida() — activa el flag y dispara el one-shot
// ================================================================
void Net::_marcarHoraValida() {
    if (!_horaValida) {
        _horaValida = true;
        _flagJustGotTime = true;
    }
}

// ================================================================
//  _iniciarPortal() — AP + DNS + WebServer
// ================================================================
void Net::_iniciarPortal() {
    Serial.printf("[net] portal cautivo iniciado — SSID: '%s'  IP: %s\n",
                  PORTAL_AP_SSID, AP_IP.toString().c_str());

    // Configurar AP sin clave
    WiFi.mode(WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
    WiFi.softAP(PORTAL_AP_SSID);

    // Escanear redes al entrar al portal (modo AP lo permite con asyncMode=false,
    // showHidden=true). Bloqueante ~1-2 s, pero ocurre solo una vez al inicio.
    Serial.println("[net] escaneando redes WiFi...");
    int n = WiFi.scanNetworks(false, true);
    _scanCache = "";
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            // Elemento de lista clickeable que copia el SSID al campo de texto
            _scanCache += "<li onclick=\"document.getElementById('ssid').value='"
                       + WiFi.SSID(i) + "'\">"
                       + WiFi.SSID(i)
                       + " <span class='sig'>(" + String(WiFi.RSSI(i)) + " dBm)</span>"
                       + "</li>";
        }
    } else {
        _scanCache = "<li><em>No se encontraron redes</em></li>";
    }

    // DNS Server: redirige todo a la IP del AP
    if (!_dns) _dns = new DNSServer();
    _dns->start(DNS_PORT, "*", AP_IP);

    // WebServer en puerto 80
    if (!_server) _server = new WebServer(80);

    // Rutas
    _server->on("/", [this]() { _handleRoot(); });
    _server->on("/save", HTTP_POST, [this]() { _handleSave(); });
    _server->on("/settime", [this]() { _handleSetTime(); });
    _server->on("/scan", [this]() { _handleScan(); });

    // Sondas de portal cautivo de Android, iOS, Windows
    _server->on("/generate_204",            [this]() { _handleCaptiveRedirect(); });
    _server->on("/gen_204",                 [this]() { _handleCaptiveRedirect(); });
    _server->on("/hotspot-detect.html",     [this]() { _handleCaptiveRedirect(); });
    _server->on("/library/test/success.html",[this]() { _handleCaptiveRedirect(); });
    _server->on("/connecttest.txt",         [this]() { _handleCaptiveRedirect(); });
    _server->on("/redirect",                [this]() { _handleCaptiveRedirect(); });
    _server->on("/canonical.html",          [this]() { _handleCaptiveRedirect(); });
    _server->on("/success.txt",             [this]() { _handleCaptiveRedirect(); });
    _server->on("/ncsi.txt",                [this]() { _handleCaptiveRedirect(); });

    // Cualquier ruta no reconocida → redirigir al portal
    _server->onNotFound([this]() { _handleCaptiveRedirect(); });

    _server->begin();
    _estado = Estado::PORTAL;
}

// ================================================================
//  _htmlPortal() — genera el HTML del portal
// ================================================================
String Net::_htmlPortal() {
    // Estilo dark minimalista; todo inline para no requerir recursos externos
    String html = R"rawhtml(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>espToy Setup</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#121212;color:#e0e0e0;font-family:sans-serif;
       display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px}
  .card{background:#1e1e1e;border-radius:16px;padding:28px 24px;
        max-width:420px;width:100%;box-shadow:0 8px 32px rgba(0,0,0,0.6)}
  h1{font-size:1.5rem;font-weight:700;margin-bottom:4px;text-align:center}
  .sub{color:#888;font-size:0.85rem;text-align:center;margin-bottom:24px}
  label{display:block;font-size:0.8rem;color:#aaa;margin-bottom:4px;margin-top:14px}
  input[type=text],input[type=password]{
    width:100%;background:#2a2a2a;border:1px solid #444;border-radius:8px;
    color:#fff;padding:10px 12px;font-size:0.95rem;outline:none}
  input:focus{border-color:#5b9cf6}
  ul#redes{list-style:none;background:#232323;border:1px solid #333;border-radius:8px;
           max-height:160px;overflow-y:auto;margin-top:8px;padding:4px 0}
  ul#redes li{padding:8px 12px;cursor:pointer;font-size:0.88rem;border-bottom:1px solid #2a2a2a}
  ul#redes li:last-child{border-bottom:none}
  ul#redes li:hover{background:#2e2e2e}
  .sig{color:#888;font-size:0.78rem}
  .btn{display:block;width:100%;margin-top:20px;padding:12px;border:none;
       border-radius:10px;font-size:1rem;font-weight:600;cursor:pointer}
  .btn-primary{background:#5b9cf6;color:#000}
  .btn-primary:hover{background:#7ab3ff}
  .btn-secondary{background:#2a2a2a;color:#ccc;border:1px solid #444;margin-top:10px}
  .btn-secondary:hover{background:#333}
  .divider{border:none;border-top:1px solid #333;margin:22px 0}
  #msg{margin-top:14px;padding:10px;border-radius:8px;font-size:0.88rem;display:none}
  .ok{background:#1a3a1a;color:#6fcf6f;border:1px solid #2d6a2d}
  .err{background:#3a1a1a;color:#cf6f6f;border:1px solid #6a2d2d}
  small{color:#666;font-size:0.75rem;display:block;margin-top:6px}
</style>
</head>
<body>
<div class="card">
  <h1>espToy 🤖</h1>
  <p class="sub">Configuración de red y hora</p>

  <!-- Sección WiFi -->
  <form method="POST" action="/save">
    <label>Redes disponibles (tocá para seleccionar)</label>
    <ul id="redes">)rawhtml";

    html += _scanCache;

    html += R"rawhtml(</ul>
    <label for="ssid">Nombre de red (SSID)</label>
    <input type="text" id="ssid" name="ssid" placeholder="Mi Red WiFi" autocomplete="off">
    <label for="pass">Contraseña</label>
    <input type="password" id="pass" name="pass" placeholder="contraseña" autocomplete="off">
    <small>Las credenciales se guardan en la memoria interna del dispositivo.</small>
    <button type="submit" class="btn btn-primary">Guardar y conectar</button>
  </form>

  <hr class="divider">

  <!-- Sección hora del teléfono -->
  <p style="font-size:0.85rem;color:#aaa;margin-bottom:12px">
    ¿Sin internet? Podés darle la hora directamente desde este teléfono.
  </p>
  <button class="btn btn-secondary" onclick="enviarHora()">
    Usar la hora de este teléfono
  </button>
  <div id="msg"></div>
</div>

<script>
function enviarHora() {
  var epoch = Math.floor(Date.now() / 1000);
  var tzmin  = new Date().getTimezoneOffset(); // minutos oeste de UTC (negativo en ARG)
  fetch('/settime?epoch=' + epoch + '&tzmin=' + tzmin)
    .then(function(r){ return r.text(); })
    .then(function(t){
      var el = document.getElementById('msg');
      el.className = 'ok';
      el.textContent = t;
      el.style.display = 'block';
    })
    .catch(function(){
      var el = document.getElementById('msg');
      el.className = 'err';
      el.textContent = 'Error al enviar la hora.';
      el.style.display = 'block';
    });
}
</script>
</body>
</html>)rawhtml";

    return html;
}

// ================================================================
//  Handlers del WebServer
// ================================================================

void Net::_handleRoot() {
    String html = _htmlPortal();
    _server->send(200, "text/html; charset=utf-8", html);
}

void Net::_handleSave() {
    String nuevoSsid = _server->arg("ssid");
    String nuevoPass = _server->arg("pass");

    nuevoSsid.trim();
    nuevoPass.trim();

    if (nuevoSsid.length() == 0) {
        _server->send(400, "text/html; charset=utf-8",
                      "<h2>Error: SSID vacío</h2><a href='/'>Volver</a>");
        return;
    }

    // Guardar en NVS
    Preferences prefs;
    prefs.begin("esptoy", false);  // lectura/escritura
    prefs.putString("wifi_ssid", nuevoSsid);
    prefs.putString("wifi_pass", nuevoPass);
    prefs.end();

    Serial.printf("[net] credenciales guardadas para SSID: '%s' — reiniciando\n",
                  nuevoSsid.c_str());

    // Responder antes de reiniciar
    _server->send(200, "text/html; charset=utf-8",
        "<!DOCTYPE html><html lang='es'><head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<style>body{background:#121212;color:#e0e0e0;font-family:sans-serif;"
        "display:flex;justify-content:center;align-items:center;height:100vh}"
        ".card{text-align:center;padding:32px;background:#1e1e1e;border-radius:16px}"
        "h2{margin-bottom:12px}p{color:#888;font-size:.9rem}</style></head>"
        "<body><div class='card'><h2>espToy 🤖</h2>"
        "<p>Credenciales guardadas.<br>El dispositivo se reiniciará y conectará a<br>"
        "<strong>" + nuevoSsid + "</strong></p></div></body></html>");

    // Breve pausa para que la respuesta llegue al navegador, luego reiniciar
    delay(1500);
    ESP.restart();
}

void Net::_handleSetTime() {
    // Parámetros: epoch (segundos UTC desde 1970) y tzmin (offset zona del cliente)
    // Nota: usamos el epoch UTC del navegador directamente.
    // configTime con TZ_OFFSET_S ya fue llamado (o se llama aquí) para que
    // getLocalTime devuelva la hora local correcta según Argentina.
    if (!_server->hasArg("epoch")) {
        _server->send(400, "text/plain", "falta parámetro epoch");
        return;
    }

    time_t epochUtc = (time_t)_server->arg("epoch").toInt();

    if (epochUtc < 1600000000UL) {
        _server->send(400, "text/plain", "epoch inválido");
        return;
    }

    // Fijar TZ offset para que getLocalTime funcione bien
    configTime(TZ_OFFSET_S, 0, "");  // sin servidor NTP, solo fija el offset

    // Setear el RTC interno con el epoch UTC recibido del navegador
    struct timeval tv;
    tv.tv_sec  = epochUtc;
    tv.tv_usec = 0;
    settimeofday(&tv, nullptr);

    // Verificar con getLocalTime
    struct tm ti;
    getLocalTime(&ti);
    Serial.printf("[net] hora recibida del teléfono — local: %02d:%02d:%02d %02d/%02d/%04d\n",
                  ti.tm_hour, ti.tm_min, ti.tm_sec,
                  ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);

    _marcarHoraValida();

    String respuesta = "Hora configurada: ";
    char buf[32];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d (hora local)",
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    respuesta += buf;

    _server->send(200, "text/plain; charset=utf-8", respuesta);
}

void Net::_handleScan() {
    // Forzar rescan y actualizar caché
    int n = WiFi.scanNetworks(false, true);
    _scanCache = "";
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            _scanCache += "<li onclick=\"document.getElementById('ssid').value='"
                       + WiFi.SSID(i) + "'\">"
                       + WiFi.SSID(i)
                       + " <span class='sig'>(" + String(WiFi.RSSI(i)) + " dBm)</span>"
                       + "</li>";
        }
    } else {
        _scanCache = "<li><em>No se encontraron redes</em></li>";
    }
    // Devolver solo el fragmento HTML de la lista para que JS lo inyecte si se desea
    _server->send(200, "text/plain; charset=utf-8", _scanCache);
}

void Net::_handleCaptiveRedirect() {
    // Responder a las sondas de portal cautivo de Android/iOS/Windows
    // con una redirección a la raíz para que el sistema operativo
    // detecte el portal y muestre la notificación.
    _server->sendHeader("Location", "http://" + AP_IP.toString() + "/", true);
    _server->send(302, "text/plain", "");
}
