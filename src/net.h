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

    // Para el menú de estado:
    bool hasCredentials() const { return _ssid.length() > 0; }
    const char* ssidGuardado() const { return _ssid.c_str(); }

private:
    // ----- Estado interno -----------------------------------------
    // Nota: el portal puede seguir activo DURANTE SINCRONIZANDO_NTP
    // (caso AP_STA: conectó en un reintento con el portal abierto).
    // Por eso "portal activo" es un flag aparte (_portalActivo) y no
    // se deduce solo del estado.
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
    uint32_t _tUltimoBegin    = 0;   // millis del último WiFi.begin() (guarda del scan)
    bool     _huboBegin       = false; // hubo al menos un WiFi.begin()

    // Reintentos de STA (portal AP_STA y modo silencioso post-portal)
    bool     _reintentoSilencioso  = false; // CONECTANDO sin volver a abrir portal
    uint8_t  _reintentosRestantes  = 0;     // tope de reintentos silenciosos
    uint32_t _tUltimoReintento     = 0;     // último WiFi.begin() del portal AP_STA

    // Reintentos en STA PURO (sin AP) antes de abrir el portal, SOLO para la
    // conexión inicial del arranque (routers lentos/erráticos). El resync
    // diario no los usa: si falla va directo a REPOSO. Se resetea al conectar.
    uint8_t  _intentosStaIniciales = 0;     // 0..3

    // Mensaje de error a mostrar en el portal (vacío = sin error)
    String _ultimoError;

    // ----- Portal cautivo -----------------------------------------
    // Punteros al heap para no consumir RAM si el portal nunca se usa.
    // Se crean UNA sola vez (con sus rutas) y se reutilizan con
    // begin()/stop(); nunca se destruyen.
    class DNSServer*   _dns    = nullptr;
    class WebServer*   _server = nullptr;

    bool     _portalActivo        = false; // AP + DNS + HTTP atendiendo
    bool     _portalConSTA        = false; // portal en AP_STA reintentando credenciales
    bool     _ntpTimeoutConectado = false; // conectado pero NTP no respondió (evita re-ciclo)
    bool     _cierrePendiente     = false; // cierre diferido programado desde un handler
    uint32_t _tCierrePortal       = 0;     // millis() a partir del cual cerrar

    String _scanCache;   // resultado del último scan de redes, ya como <li>...

    // ----- Métodos privados ----------------------------------------
    void _cargarCredenciales();
    void _iniciarConexion();
    void _iniciarPortal();
    void _detenerPortal();               // softAPdisconnect + stop de server/dns
    void _atenderPortal();               // dns + http + recolección del scan async
    void _lanzarScan();                  // scan async (nunca bloquea)
    void _regenerarScanCache(int n);     // arma los <li> desde los resultados
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
