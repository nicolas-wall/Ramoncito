// =============================================================
//  espToy — net.h
//  Módulo de red: WiFi, NTP, hora local, ciclo día/noche,
//  portal cautivo de configuración.
//  Plataforma: Seeed XIAO ESP32-S3 / arduino-esp32 2.0.17
// =============================================================
#pragma once
#include <Arduino.h>
#include <time.h>

class Net {
public:
    // Carga credenciales de NVS y arranca el intento de conexión (no bloquea).
    void begin();

    // Máquina de estados: conexión, NTP, portal, resync.
    // Llamar en cada frame (o cada loop); sin delay() internos.
    void update(uint32_t now);

    // true si hay hora confiable (NTP sincronizado o recibida del teléfono).
    bool timeValid() const;

    // true si la hora local está dentro de la ventana nocturna
    // (HORA_DORMIR..HORA_DESPERTAR), respetando hora forzada.
    // false si no hay hora válida y no hay hora forzada.
    bool isNight() const;

    // Hora local 0-23. Si hay hora forzada la devuelve.
    // Si no hay hora válida devuelve -1.
    int hourNow() const;

    // Fuerza la hora para tests/comandos seriales. -1 desactiva el forzado.
    void forceHour(int h);

    // true si el portal cautivo está activo.
    bool portalActive() const;

    // true UNA sola vez cuando la hora pasa de inválida a válida.
    // (Para que main.cpp aplique el decaimiento offline.)
    bool justGotValidTime();

    // Fuerza el inicio del portal manualmente.
    void startPortal();

private:
    // ----- Estado interno -----------------------------------------
    enum class Estado : uint8_t {
        SIN_CREDENCIALES,   // sin ssid/pass en NVS → portal
        CONECTANDO,         // WiFi.begin() en curso, esperando WL_CONNECTED
        SINCRONIZANDO_NTP,  // conectado; esperando que time() sea válido
        REPOSO,             // hora obtenida (o no), WiFi apagado; vive el resync
        PORTAL              // AP + DNS + WebServer activos
    };

    Estado   _estado          = Estado::SIN_CREDENCIALES;
    bool     _horaValida      = false;   // hora real confiable (NTP o teléfono)
    int      _horaForzada     = -1;      // -1 = sin forzado
    bool     _flagJustGotTime = false;   // one-shot para justGotValidTime()

    // Credenciales cargadas de NVS
    String _ssid;
    String _pass;

    // Timestamps de millis() para timeouts y resync
    uint32_t _tInicioConexion = 0;   // cuándo empezó CONECTANDO
    uint32_t _tInicioNTP      = 0;   // cuándo empezó SINCRONIZANDO_NTP
    uint32_t _tUltimaSync     = 0;   // millis de la última sincronización exitosa

    // ----- Portal cautivo -----------------------------------------
    // Usando punteros a objetos del heap para no consumir RAM si el portal
    // nunca se usa. Se crean una sola vez y permanecen hasta reinicio.
    class DNSServer*   _dns    = nullptr;
    class WebServer*   _server = nullptr;

    String _scanCache;   // resultado del scan de redes WiFi cacheado en HTML

    // ----- Métodos privados ----------------------------------------
    void _cargarCredenciales();
    void _iniciarConexion();
    void _iniciarPortal();
    void _cerrarPortal();
    void _marcarHoraValida();

    // Handlers del WebServer
    void _handleRoot();
    void _handleSave();
    void _handleSetTime();
    void _handleCaptiveRedirect();
    void _handleScan();

    // Genera el HTML del portal (strings en RAM; se descarta tras enviar)
    String _htmlPortal();
};

extern Net net;
