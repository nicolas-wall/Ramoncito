// =============================================================
//  Ramoncito — ota.h
//  Auto-OTA por internet: CHEQUEO automático de versión nueva en
//  GitHub Releases. La instalación NUNCA es automática: la
//  confirma el usuario (menú → instalarAhora()).
//
//  Flujo:
//    1. 90 s tras el boot: GET version.json → comparar con FW_VERSION.
//       Después, un chequeo cada 24 h.
//    2. Si hay versión nueva: se guarda (versión + md5) y se expone
//       por hayActualizacion()/versionNueva(). NO se descarga nada.
//    3. Cuando el usuario confirma: instalarAhora() descarga el
//       firmware, lo flashea (con verificación MD5) y reinicia.
//    4. Si la instalación falla: la actualización sigue marcada como
//       disponible y se puede reintentar.
//
//  Prerequisito: net.staConnected() == true (necesita internet).
//  Plataforma: Seeed XIAO ESP32-S3 / arduino-esp32 2.0.17
// =============================================================
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

class Ota {
public:
    // Guarda el puntero al display para la pantalla de progreso.
    // Llamar desde setup() después de u8g2.begin().
    void begin(U8G2* display);

    // Chequeo automático de versión: llamar en cada frame desde loop().
    // Solo CHEQUEA (bloqueante durante el GET); nunca instala solo.
    void update(uint32_t now);

    // Marca el próximo chequeo para YA (lo usa el comando serial 'u').
    void forzarChequeo();

    // true si el último chequeo encontró una versión más nueva que
    // FW_VERSION (pendiente de instalar por confirmación del usuario).
    bool hayActualizacion() const { return _hayNueva; }

    // Versión nueva disponible (ej. "0.9.1"); string vacío si no hay.
    const char* versionNueva() const;

    // Descarga e instala la actualización pendiente (bloqueante:
    // pantalla de progreso + restart si OK). Solo actúa si hay una
    // actualización disponible y la STA está conectada; si no, loguea
    // y retorna. La llama el menú cuando el usuario confirma.
    void instalarAhora();

    // String corto del último resultado para el menú o diagnóstico.
    // Posibles valores: "sin chequear", "al dia", "disponible",
    //                   "error http", "error json", "error flash",
    //                   "actualizado"
    const char* estadoTexto() const;

private:
    U8G2*    _display        = nullptr;
    uint32_t _proximoChequeo = 0;      // millis() del próximo chequeo (0 = no programado aún)
    bool     _iniciado       = false;

    // Actualización pendiente (encontrada por _chequear(), a la espera
    // de la confirmación del usuario desde el menú)
    bool _hayNueva         = false;
    char _versionNueva[16] = {0};      // "major.minor.patch"
    char _md5Nueva[36]     = {0};      // 32 hex + margen; vacío si el JSON no lo trajo

    // Último resultado del chequeo/instalación
    enum class Estado : uint8_t {
        SIN_CHEQUEAR,
        AL_DIA,
        DISPONIBLE,     // hay versión nueva esperando confirmación
        ERROR_HTTP,
        ERROR_JSON,
        ERROR_FLASH,
        ACTUALIZADO
    };
    Estado _estado = Estado::SIN_CHEQUEAR;

    // Chequeo de versión (bloqueante durante el GET): descarga y parsea
    // version.json, compara con FW_VERSION y guarda versión/md5 si hay
    // una más nueva. NO descarga el firmware.
    void _chequear();

    // Instalación (bloqueante): descarga firmware.bin, lo flashea con
    // verificación MD5, muestra el progreso en OLED y reinicia si OK.
    // Usa _versionNueva/_md5Nueva guardados por _chequear(). Si falla,
    // _hayNueva queda en true para poder reintentar.
    void _instalar();

    // Parsea "major.minor.patch" y devuelve un entero comparable.
    // Ignora cualquier sufijo tras los números (ej. "-interaccion").
    // Devuelve 0 si el formato no es válido.
    static uint32_t _parsearVersion(const char* ver);

    // Dibuja la pantalla de progreso de descarga en el OLED.
    // progreso: 0–100
    void _dibujarProgreso(const char* verNueva, int progreso);
};

extern Ota ota;
