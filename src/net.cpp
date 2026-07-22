// =============================================================
//  Ramoncito — net.cpp
//  Implementación del módulo de red.
//
//  FILOSOFÍA: el AP de setup "Ramoncito-setup" debe estar SIEMPRE encendido y
//  visible desde el segundo 1, pase lo que pase con la conexión STA. Con
//  credenciales, el arranque va DIRECTO a PORTAL en AP_STA (softAP + DNS +
//  HTTP atendiendo) y la STA se reintenta de fondo. El AP no se apaga nunca
//  hasta que NTP confirme hora válida. Si nunca conecta, el AP queda
//  encendido indefinidamente para que el usuario pueda configurar o dar la
//  hora del teléfono.
//
//  Máquina de estados:
//    begin() con credenciales → PORTAL (AP_STA, softAP arriba + STA de fondo)
//    begin() sin credenciales → PORTAL (AP puro)
//    PORTAL(AP_STA)   → SINCRONIZANDO_NTP  (la STA de fondo conectó; el portal
//                       SIGUE atendiendo y el AP encendido hasta que NTP OK)
//    SINCRONIZANDO_NTP→ REPOSO             (hora válida; cierra portal + AP off)
//    SINCRONIZANDO_NTP→ PORTAL             (timeout NTP con portal activo:
//                       conectado pero sin internet → usar hora del teléfono)
//    PORTAL           → cierre DIFERIDO tras /settime (flag + timestamp,
//                       procesado en update(); nunca dentro de un handler).
//                       Si quedan credenciales sin conectar, pasa a CONECTANDO
//                       en modo silencioso (STA sola, ya sin AP).
//    CONECTANDO       → SINCRONIZANDO_NTP  (silencioso post-/settime conectó)
//    CONECTANDO       → REPOSO             (silencioso agotado, o resync fallido)
//    REPOSO           → CONECTANDO         (resync diario NTP_RESYNC_MS)
//
//  El scan de redes es SIEMPRE asíncrono (WiFi.scanNetworks(true, true)):
//  nunca se bloquea el loop, la cara sigue animando a 30 fps.
//
//  IMPORTANTE (estabilidad del AP en AP_STA): cada WiFi.begin() y cada scan
//  cambian el canal de la radio y cortan los beacons del AP unos ms. Por eso
//  con credenciales el scan NUNCA es automático (solo a pedido del botón del
//  portal) y el reintento de STA de fondo va espaciado (cada 30 s), de modo
//  que el AP esté en el aire la enorme mayoría del tiempo.
//
//  El portal cautivo responde también a las sondas de Android/iOS para
//  que el teléfono muestre la notificación de "iniciar sesión".
//
//  ACTUALIZACIÓN OTA: conectarse al AP Ramoncito-setup → http://192.168.4.1/update
//  → usuario/clave ramoncito/ramoncito → elegir firmware.bin y pulsar "Actualizar".
// =============================================================

#include "net.h"
#include "config.h"

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>      // ramoncito.local en la LAN
#include <Preferences.h>
#include <Update.h>       // OTA por web

// Instancia global accesible desde main.cpp
Net net;

// Constantes internas del módulo
static const uint32_t NTP_TIMEOUT_MS       = 15000;  // 15 s esperando NTP
static const uint32_t REINTENTO_STA_MS     = 30000;  // reintento de STA de fondo c/30 s
                                                     // (el AP queda fijo arriba; cada begin()
                                                     // solo corta beacons unos ms, así que
                                                     // el AP sigue visible casi todo el tiempo)
static const uint32_t CIERRE_PORTAL_MS     = 3000;   // demora del cierre tras /settime
static const uint32_t SCAN_GUARDA_BEGIN_MS = 20000;  // ventana de asociación tranquila:
                                                     // sin scans hasta 20 s tras un begin()
                                                     // (asociar puede tardar >10 s)
static const uint8_t  MAX_REINTENTOS_SILENCIOSOS = 10; // ~10 min y desiste

// Diagnóstico: loguear el wl_status_t una sola vez por reintento.
// Códigos útiles: 0=IDLE, 1=NO_SSID_AVAIL (no ve la red), 4=CONNECT_FAILED
// (clave mal), 5=CONNECTION_LOST, 6=DISCONNECTED.
static bool s_statusLogueado = true;   // true = nada pendiente de loguear
static const uint8_t  DNS_PORT             = 53;
static const IPAddress AP_IP               (192, 168, 4, 1);
static const IPAddress AP_SUBNET           (255, 255, 255, 0);

// ================================================================
//  begin() — llamar desde setup()
// ================================================================
void Net::begin() {
    // Las credenciales las guardamos nosotros en NVS ("ramoncito"); no
    // necesitamos que el driver WiFi las persista en su flash en cada
    // WiFi.begin() (los reintentos de reconexión escribirían flash de más).
    WiFi.persistent(false);

    // ---- Modo SOLO-AP (WIFI_INTENTAR_STA == false) -----------------
    // No se intenta conectar a ninguna red: se levanta SOLO el AP de setup,
    // estable y permanente, y la hora llega por /settime desde el teléfono.
    // Sin cargar credenciales, sin WiFi.begin(), sin scans, sin reintentos.
    if (!WIFI_INTENTAR_STA) {
        Serial.println("[net] modo SOLO-AP (WIFI_INTENTAR_STA=false) → AP puro estable, sin STA");
        _iniciarPortal();   // _ssid vacío ⇒ _portalConSTA=false ⇒ WIFI_AP puro
        return;
    }

    // ---- Modo STA/NTP (WIFI_INTENTAR_STA == true) ------------------
    _cargarCredenciales();

    // SIEMPRE arrancar el portal desde el segundo 1:
    //  - con credenciales → AP_STA: softAP visible + STA reintentando de fondo.
    //  - sin credenciales → AP puro.
    // El AP no se apaga hasta que NTP confirme hora válida (o nunca, si no
    // conecta: el usuario lo necesita para configurar / dar la hora).
    if (_ssid.length() > 0) {
        Serial.println("[net] credenciales presentes → portal AP_STA (AP visible + STA de fondo)");
        // Disparamos ya el primer intento STA; _iniciarPortal() añade luego el
        // AP con WiFi.mode(WIFI_AP_STA) sin cancelarlo. Los reintentos de
        // fondo (cada 30 s) los maneja el estado PORTAL.
        WiFi.begin(_ssid.c_str(), _pass.c_str());
        _tInicioConexion = millis();
        _tUltimoBegin    = _tInicioConexion;
        _huboBegin       = true;
    } else {
        Serial.println("[net] sin credenciales guardadas → iniciando portal (AP)");
    }
    _iniciarPortal();
}

// ================================================================
//  update() — llamar en cada frame/loop, SIN delay()
// ================================================================
void Net::update(uint32_t now) {
    // Panel LAN: atender el HTTP sobre la STA fuera del portal (el portal ya
    // llama a handleClient() en _atenderPortal(); acá cubrimos REPOSO y el
    // resync). Barato: sin clientes, handleClient() retorna enseguida.
    if (_lanServer && !_portalActivo && _server) _server->handleClient();

    switch (_estado) {

        // ---- CONECTANDO ------------------------------------------
        // Sin AP. Solo se llega acá por dos vías, ambas post-portal:
        //  (a) resync diario desde REPOSO (_iniciarConexion, hora ya válida);
        //  (b) modo silencioso tras /settime (portal cerrado, STA sola).
        // El arranque inicial NUNCA entra acá: va directo a PORTAL (AP_STA).
        case Estado::CONECTANDO: {
            wl_status_t st = WiFi.status();
            if (st == WL_CONNECTED) {
                Serial.printf("[net] conectado a '%s' IP: %s\n",
                              _ssid.c_str(),
                              WiFi.localIP().toString().c_str());
                _ultimoError = "";            // conexión OK → limpiar error
                _reintentoSilencioso = false;
                // Configurar zona horaria y disparar NTP
                configTime(TZ_OFFSET_S, 0, NTP_SERVER);
                _tInicioNTP = now;
                _estado = Estado::SINCRONIZANDO_NTP;

            } else if ((now - _tInicioConexion) >= WIFI_TIMEOUT_MS) {

                if (_reintentoSilencioso) {
                    // Diagnóstico: resultado del último reintento, una sola vez,
                    // recién cuando la ventana de asociación (20 s) ya venció.
                    if (!s_statusLogueado &&
                        (now - _tUltimoBegin) >= SCAN_GUARDA_BEGIN_MS) {
                        Serial.printf("[net] status tras reintento: %d\n", (int)st);
                        s_statusLogueado = true;
                    }
                    // Modo silencioso (post-/settime): reintentar cada
                    // REINTENTO_STA_MS sin AP, con tope de intentos.
                    if ((now - _tUltimoBegin) >= REINTENTO_STA_MS) {
                        if (_reintentosRestantes == 0) {
                            Serial.println("[net] reintentos silenciosos agotados — WiFi off");
                            WiFi.disconnect(true);
                            WiFi.mode(WIFI_OFF);
                            // Si hay hora válida, el resync diario volverá a intentar
                            if (_horaValida && _tUltimaSync == 0) _tUltimaSync = now;
                            _reintentoSilencioso = false;
                            _estado = Estado::REPOSO;
                        } else {
                            _reintentosRestantes--;
                            Serial.printf("[net] reintento silencioso de conexión a '%s' (quedan %u)\n",
                                          _ssid.c_str(), _reintentosRestantes);
                            WiFi.disconnect(false);  // limpiar el estado del supplicant
                            WiFi.begin(_ssid.c_str(), _pass.c_str());
                            _tUltimoBegin = now;
                            _huboBegin = true;
                            s_statusLogueado = false;  // pendiente de diagnóstico
                        }
                    }

                } else {
                    // Falló un resync diario: NO molestar con el portal,
                    // el equipo ya funciona con hora. Reintentar en 24 h.
                    Serial.printf("[net] resync: timeout de conexión a '%s' — reintento en 24 h\n",
                                  _ssid.c_str());
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                    _tUltimaSync = now;   // corre la ventana del próximo resync
                    _estado = Estado::REPOSO;
                }
            }
            break;
        }

        // ---- SINCRONIZANDO_NTP -----------------------------------
        case Estado::SINCRONIZANDO_NTP: {
            // Si el portal sigue abierto (conectó durante AP_STA), seguir
            // atendiéndolo para no dejar colgado al teléfono.
            if (_portalActivo) _atenderPortal();

            time_t ahora = time(nullptr);
            if (ahora > 1600000000UL) {
                // Hora válida obtenida por NTP
                struct tm ti;
                getLocalTime(&ti);
                Serial.printf("[net] NTP ok — hora local: %02d:%02d:%02d %02d/%02d/%04d\n",
                              ti.tm_hour, ti.tm_min, ti.tm_sec,
                              ti.tm_mday, ti.tm_mon + 1, ti.tm_year + 1900);
                _tUltimaSync = now;
                _ultimoError = "";
                // El portal ya cumplió: cerrarlo (conservando el HTTP si vamos
                // a servir el panel LAN sobre la STA).
                if (_portalActivo) _detenerPortal(PANEL_LAN_HABILITADO);
                if (OTA_AUTO_HABILITADO || PANEL_LAN_HABILITADO) {
                    // El auto-OTA y el panel LAN necesitan la STA viva (solo se
                    // baja el AP). Costo: algo más de consumo y de ruido en el
                    // touch; se revisará con la versión a batería.
                    WiFi.mode(WIFI_STA);
                    _iniciarLanServer();
                } else {
                    // Apagar WiFi para ahorrar energía y reducir ruido táctil
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                }
                _marcarHoraValida();
                _estado = Estado::REPOSO;

            } else if ((now - _tInicioNTP) >= NTP_TIMEOUT_MS) {
                if (_portalActivo) {
                    // Conectados pero sin respuesta NTP (¿sin internet?).
                    // Volver al portal para que el usuario pueda usar la hora
                    // del teléfono. SNTP sigue reintentando en background.
                    Serial.println("[net] advertencia: timeout NTP con portal activo — vuelvo al portal");
                    _ntpTimeoutConectado = true;
                    _ultimoError = "Me conecté a '" + _ssid +
                                   "' pero no pude obtener la hora de internet. "
                                   "Podés usar la hora de este teléfono.";
                    // El AP nunca se apagó: seguimos atendiendo el portal.
                    _estado = Estado::PORTAL;
                } else {
                    Serial.println("[net] advertencia: timeout esperando NTP — sin hora confiable");
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                    _estado = Estado::REPOSO;
                }
            }
            break;
        }

        // ---- REPOSO ----------------------------------------------
        case Estado::REPOSO: {
            // Watchdog de la STA: mientras el panel LAN / auto-OTA necesiten
            // la conexión viva, vigilar que no se caiga. Si se cae (WiFi
            // inestable), reintentar y re-anunciar mDNS al volver. Sin esto,
            // una caída dejaba el panel muerto hasta el resync de 24 h.
            if ((PANEL_LAN_HABILITADO || OTA_AUTO_HABILITADO) && _ssid.length() > 0) {
                if (WiFi.status() == WL_CONNECTED) {
                    if (!_lanServer) {
                        Serial.printf("[net] STA reconectada — IP %s\n",
                                      WiFi.localIP().toString().c_str());
                        _iniciarLanServer();   // re-begin server + re-anuncia mDNS
                    }
                } else {
                    if (_lanServer) {
                        // Recién se cayó: bajar el anuncio mDNS (la IP puede
                        // cambiar al volver) y marcar el server como no-activo.
                        Serial.println("[net] STA caida en reposo — reintentando reconexion");
                        MDNS.end();
                        _mdnsIniciado = false;
                        _lanServer    = false;
                    }
                    // Reintento activo cada REINTENTO_STA_MS (además del
                    // auto-reconnect del supplicant) para forzar la vuelta.
                    if ((now - _tUltimoReintento) >= REINTENTO_STA_MS) {
                        _tUltimoReintento = now;
                        WiFi.begin(_ssid.c_str(), _pass.c_str());
                    }
                }
            }

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
            _atenderPortal();

            // Cierre diferido programado por /settime (nunca dentro del handler)
            if (_cierrePendiente && (int32_t)(now - _tCierrePortal) >= 0) {
                bool conectado = (WiFi.status() == WL_CONNECTED);
                // Conservar el HTTP solo si vamos a servir el panel LAN (STA viva)
                _detenerPortal(PANEL_LAN_HABILITADO && conectado);
                if (_portalConSTA && !conectado) {
                    // Quedan credenciales sin conectar: seguir reintentando
                    // la STA en silencio, ya sin AP.
                    Serial.println("[net] portal cerrado — sigo reintentando la conexión en silencio");
                    WiFi.mode(WIFI_STA);
                    _reintentoSilencioso = true;
                    _reintentosRestantes = MAX_REINTENTOS_SILENCIOSOS;
                    _tInicioConexion = now - WIFI_TIMEOUT_MS; // timeout ya vencido: gate por _tUltimoBegin
                    _estado = Estado::CONECTANDO;
                } else if ((OTA_AUTO_HABILITADO || PANEL_LAN_HABILITADO) && conectado) {
                    // STA ya conectada: mantenerla viva para el auto-OTA y el panel
                    WiFi.mode(WIFI_STA);
                    _iniciarLanServer();
                    _estado = Estado::REPOSO;
                } else {
                    WiFi.disconnect(true);
                    WiFi.mode(WIFI_OFF);
                    _estado = Estado::REPOSO;
                }
                break;
            }

            // Reintentos de STA con el portal activo (modo AP_STA)
            if (_portalConSTA) {
                wl_status_t st = WiFi.status();

                if (st == WL_CONNECTED && !_ntpTimeoutConectado) {
                    // ¡Un reintento conectó! Pasar a NTP; el portal sigue
                    // atendiendo (lo cierra el éxito de NTP).
                    Serial.printf("[net] conectado a '%s' durante el portal — IP: %s\n",
                                  _ssid.c_str(), WiFi.localIP().toString().c_str());
                    _ultimoError = "";
                    configTime(TZ_OFFSET_S, 0, NTP_SERVER);
                    _tInicioNTP = now;
                    _estado = Estado::SINCRONIZANDO_NTP;

                } else if (st != WL_CONNECTED) {
                    // Si se cayó la conexión, rehabilitar el ciclo de NTP
                    _ntpTimeoutConectado = false;
                    // Diagnóstico: resultado del último reintento, una sola vez,
                    // recién cuando la ventana de asociación (20 s) ya venció.
                    if (!s_statusLogueado &&
                        (now - _tUltimoBegin) >= SCAN_GUARDA_BEGIN_MS) {
                        Serial.printf("[net] status tras reintento: %d\n", (int)st);
                        s_statusLogueado = true;
                    }
                    // Reintento de STA de fondo cada 30 s. El AP queda fijo
                    // arriba; cada begin() solo corta beacons unos ms.
                    // No pisar un scan en curso.
                    if ((now - _tUltimoReintento) >= REINTENTO_STA_MS &&
                        WiFi.scanComplete() != WIFI_SCAN_RUNNING) {
                        Serial.printf("[net] reintento de conexión a '%s' (portal activo, AP fijo)\n",
                                      _ssid.c_str());
                        WiFi.disconnect(false);  // limpiar el estado del supplicant
                        WiFi.begin(_ssid.c_str(), _pass.c_str());
                        _tUltimoReintento = now;
                        _tUltimoBegin     = now;
                        _huboBegin        = true;
                        s_statusLogueado  = false;  // pendiente de diagnóstico
                    }

                } else if (_ntpTimeoutConectado) {
                    // Conectado pero NTP había fallado: SNTP reintenta en
                    // background; si la hora llega tarde, aprovecharla.
                    if (time(nullptr) > 1600000000UL) {
                        struct tm ti;
                        getLocalTime(&ti);
                        Serial.printf("[net] NTP llegó tarde — hora local: %02d:%02d:%02d\n",
                                      ti.tm_hour, ti.tm_min, ti.tm_sec);
                        _tUltimaSync = now;
                        _ultimoError = "";
                        _marcarHoraValida();
                        _detenerPortal(PANEL_LAN_HABILITADO);
                        if (OTA_AUTO_HABILITADO || PANEL_LAN_HABILITADO) {
                            WiFi.mode(WIFI_STA);   // mantener STA para el auto-OTA y el panel
                            _iniciarLanServer();
                        } else {
                            WiFi.disconnect(true);
                            WiFi.mode(WIFI_OFF);
                        }
                        _estado = Estado::REPOSO;
                    }
                }
            }
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
//  portalActive() — refleja el flag real (el portal puede seguir
//  activo durante SINCRONIZANDO_NTP en el flujo AP_STA)
// ================================================================
bool Net::portalActive() const {
    return _portalActivo;
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
    if (_portalActivo) return;  // ya está activo
    _iniciarPortal();           // AP_STA si hay credenciales, AP si no
}

// ================================================================
//  staConnected() — true si la STA está asociada a un AP
//  (indica que hay internet disponible para el auto-OTA)
// ================================================================
bool Net::staConnected() const {
    return WiFi.status() == WL_CONNECTED;
}

// ================================================================
//  _cargarCredenciales() — leer ssid/pass de NVS
// ================================================================
void Net::_cargarCredenciales() {
    Preferences prefs;
    prefs.begin("ramoncito", true);  // solo lectura
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
    _tUltimoBegin    = _tInicioConexion;
    _huboBegin       = true;
    _reintentoSilencioso = false;
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
//  _iniciarPortal() — AP (o AP_STA si hay credenciales) + DNS + HTTP
// ================================================================
void Net::_iniciarPortal() {
    // Con credenciales guardadas el portal corre en AP_STA para poder
    // seguir reintentando la conexión de fondo mientras el usuario configura.
    // El softAP se levanta SIEMPRE y no se apaga hasta que NTP confirme.
    _portalConSTA = (_ssid.length() > 0);

    // WIFI_AP_STA preserva el intento STA que begin() ya pudo haber lanzado.
    WiFi.mode(_portalConSTA ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, AP_SUBNET);
    WiFi.softAP(PORTAL_AP_SSID);   // AP abierto, sin clave

    Serial.printf("[net] portal cautivo iniciado — SSID: '%s'  IP: %s  modo: %s\n",
                  PORTAL_AP_SSID, AP_IP.toString().c_str(),
                  _portalConSTA ? "AP_STA (AP fijo + STA de fondo)" : "AP");

    // Banner informativo mientras la STA de fondo aún no conectó.
    if (_portalConSTA && _ultimoError.length() == 0) {
        _ultimoError = "Intentando conectar a '" + _ssid +
                       "' de fondo... Si no conecta, revisá la clave. "
                       "Verificá que sea una red de 2.4 GHz — la de 5 GHz no sirve.";
    }

    // Scan de redes ASÍNCRONO (resultados en _atenderPortal() vía
    // WiFi.scanComplete(); la cara nunca se congela).
    // SOLO automático en portal puro AP con STA habilitada: en AP_STA cada
    // scan cambia de canal y corta los beacons del AP + pisa los reintentos.
    // En modo SOLO-AP (WIFI_INTENTAR_STA=false) NUNCA se escanea: el AP debe
    // quedar rock-solid y no hay red que elegir (la hora llega por /settime).
    _scanCache = "";
    if (WIFI_INTENTAR_STA && !_portalConSTA) _lanzarScan();

    // DNS Server: redirige todo a la IP del AP
    if (!_dns) _dns = new DNSServer();
    _dns->start(DNS_PORT, "*", AP_IP);

    // WebServer: crear y registrar rutas UNA sola vez (re-registrar en
    // cada apertura duplicaría los handlers); luego solo begin()/stop().
    _registrarRutas();
    _server->begin();

    _cierrePendiente     = false;
    _ntpTimeoutConectado = false;
    _tUltimoReintento    = millis();   // primer reintento STA de fondo a los 30 s
    _portalActivo        = true;
    _estado = Estado::PORTAL;
}

// ================================================================
//  _registrarRutas() — crea _server y registra TODOS los handlers una
//  sola vez (portal + OTA + panel LAN + sondas cautivas). Reutilizado
//  por _iniciarPortal() y _iniciarLanServer(); re-registrar duplicaría
//  handlers, así que el cuerpo entero se guarda con !_server.
// ================================================================
void Net::_registrarRutas() {
    if (_server) return;   // ya creado y con rutas
    _server = new WebServer(80);

    _server->on("/", [this]() { _handleRoot(); });
    _server->on("/save", HTTP_POST, [this]() { _handleSave(); });
    _server->on("/settime", [this]() { _handleSetTime(); });
    _server->on("/scan", [this]() { _handleScan(); });

    // Rutas OTA (solo si habilitado en config.h)
    if (OTA_HABILITADO) {
        _server->on("/update", HTTP_GET,  [this]() { _handleOtaGet(); });
        _server->on("/update", HTTP_POST,
            [this]() { _handleOtaPost(); },
            [this]() { _handleOtaUpload(); }
        );
    }

    // Panel web en la LAN (dashboard + API de estado y acciones)
    if (PANEL_LAN_HABILITADO) {
        _server->on("/panel",       [this]() { _handlePanel(); });
        _server->on("/api/state",   [this]() { _handleApiState(); });
        _server->on("/api/action",  [this]() { _handleApiAction(); });
    }

    // Sondas de portal cautivo de Android, iOS, Windows
    _server->on("/generate_204",             [this]() { _handleCaptiveRedirect(); });
    _server->on("/gen_204",                  [this]() { _handleCaptiveRedirect(); });
    _server->on("/hotspot-detect.html",      [this]() { _handleCaptiveRedirect(); });
    _server->on("/library/test/success.html",[this]() { _handleCaptiveRedirect(); });
    _server->on("/connecttest.txt",          [this]() { _handleCaptiveRedirect(); });
    _server->on("/redirect",                 [this]() { _handleCaptiveRedirect(); });
    _server->on("/canonical.html",           [this]() { _handleCaptiveRedirect(); });
    _server->on("/success.txt",              [this]() { _handleCaptiveRedirect(); });
    _server->on("/ncsi.txt",                 [this]() { _handleCaptiveRedirect(); });

    // Cualquier ruta no reconocida → redirigir a la raíz (portal o panel)
    _server->onNotFound([this]() { _handleCaptiveRedirect(); });
}

// ================================================================
//  _iniciarLanServer() — mantiene el HTTP escuchando sobre la STA (tu
//  WiFi de casa) para el panel del teléfono, y publica ramoncito.local por
//  mDNS. Se llama cuando el portal se cierra pero la STA queda viva.
//  Idempotente: begin()/MDNS.begin() se pueden repetir sin efecto.
// ================================================================
void Net::_iniciarLanServer() {
    if (!PANEL_LAN_HABILITADO) return;
    if (WiFi.status() != WL_CONNECTED) return;

    // Enchufado + panel siempre disponible → priorizar estabilidad sobre
    // consumo: apagar el modem-sleep (causa cortes/latencia con muchos
    // routers) y dejar que el supplicant reconecte solo si se cae.
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);

    _registrarRutas();     // no-op si ya existían
    _server->begin();      // reanuda la escucha (ahora sobre la STA)

    // mDNS: http://ramoncito.local sin tener que cazar la IP.
    if (!_mdnsIniciado) {
        if (MDNS.begin(PANEL_MDNS_HOST)) {
            MDNS.addService("http", "tcp", 80);
            _mdnsIniciado = true;
            Serial.printf("[net] panel LAN activo — http://%s.local  (IP %s)\n",
                          PANEL_MDNS_HOST, WiFi.localIP().toString().c_str());
        } else {
            Serial.println("[net] advertencia: no pude iniciar mDNS (uso solo la IP)");
        }
    }
    _lanServer = true;
}

// ================================================================
//  _detenerPortal() — apaga AP y DNS. El HTTP se conserva si
//  mantenerServer=true (para seguir sirviendo el panel sobre la STA).
//  NO destruye los objetos (se reutilizan). Llamar SIEMPRE desde
//  update(), nunca desde un handler del WebServer.
// ================================================================
void Net::_detenerPortal(bool mantenerServer) {
    if (!_portalActivo) return;
    if (_dns) _dns->stop();
    if (_server && !mantenerServer) _server->stop();
    WiFi.softAPdisconnect(true);
    _portalActivo    = false;
    _cierrePendiente = false;
    Serial.printf("[net] portal cautivo detenido%s\n",
                  mantenerServer ? " (HTTP sigue en la STA para el panel)" : "");
}

// ================================================================
//  _atenderPortal() — DNS + HTTP + recolección del scan async.
//  Se llama desde PORTAL y también desde SINCRONIZANDO_NTP si el
//  portal sigue abierto. Nunca bloquea más que unos ms.
// ================================================================
void Net::_atenderPortal() {
    if (_dns)    _dns->processNextRequest();
    if (_server) _server->handleClient();

    // ¿Terminó el scan asíncrono?
    int n = WiFi.scanComplete();
    if (n >= 0) {
        _regenerarScanCache(n);
        WiFi.scanDelete();
        Serial.printf("[net] scan completado: %d redes\n", n);
    } else if (WIFI_INTENTAR_STA && n == WIFI_SCAN_FAILED &&
               _scanCache.length() == 0 && _ssid.length() == 0) {
        // Relanzamiento automático SOLO sin credenciales (portal puro AP)
        // y con STA habilitada: en AP_STA los scans automáticos vuelven
        // invisible al AP y pisan los reintentos. Con credenciales, el scan
        // es solo a pedido. En modo SOLO-AP no se escanea nunca.
        _lanzarScan();
    }
}

// ================================================================
//  _lanzarScan() — scan asíncrono de redes. No hace nada si ya hay
//  un scan corriendo o si hubo un WiFi.begin() hace <20 s (en AP_STA
//  un scan pisa el intento de asociación y corta los beacons del AP).
// ================================================================
void Net::_lanzarScan() {
    // Modo SOLO-AP: NUNCA escanear (el AP puro debe quedar estable y no hay
    // red que elegir). Gate duro que cubre todos los llamadores.
    if (!WIFI_INTENTAR_STA) return;

    // Backoff: máximo un lanzamiento cada 10 s. En AP_STA, mientras la STA
    // intenta asociarse, scanComplete() devuelve FAILED y sin esta guarda
    // se relanza en cada frame (spam de log + radio ocupada).
    static uint32_t tUltimoLanzamiento = 0;
    uint32_t ahora = millis();
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) return;
    if (_huboBegin && (ahora - _tUltimoBegin) < SCAN_GUARDA_BEGIN_MS) return;
    if (tUltimoLanzamiento != 0 && (ahora - tUltimoLanzamiento) < 10000) return;
    tUltimoLanzamiento = ahora;
    WiFi.scanNetworks(true, true);   // async=true, incluir redes ocultas
    Serial.println("[net] scan de redes lanzado (asincrónico)");
}

// ================================================================
//  _regenerarScanCache() — arma la lista <li> desde los resultados
// ================================================================
void Net::_regenerarScanCache(int n) {
    _scanCache = "";
    if (n > 0) {
        for (int i = 0; i < n; i++) {
            // Elemento clickeable que copia el SSID al campo de texto
            _scanCache += "<li onclick=\"document.getElementById('ssid').value='"
                       + WiFi.SSID(i) + "'\">"
                       + WiFi.SSID(i)
                       + " <span class='sig'>(" + String(WiFi.RSSI(i)) + " dBm)</span>"
                       + "</li>";
        }
    } else {
        _scanCache = "<li><em>No se encontraron redes</em></li>";
    }
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
<title>Ramoncito Setup</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0}
  body{background:#121212;color:#e0e0e0;font-family:sans-serif;
       display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px}
  .card{background:#1e1e1e;border-radius:16px;padding:28px 24px;
        max-width:420px;width:100%;box-shadow:0 8px 32px rgba(0,0,0,0.6)}
  h1{font-size:1.5rem;font-weight:700;margin-bottom:4px;text-align:center}
  .sub{color:#888;font-size:0.85rem;text-align:center;margin-bottom:24px}
  .banner{background:#3a2a15;color:#ffb86b;border:1px solid #7a5426;
          border-radius:8px;padding:10px 12px;font-size:0.85rem;margin-bottom:16px}
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
  .btn-mini{margin-top:8px;padding:8px;font-size:0.85rem;font-weight:400}
  .divider{border:none;border-top:1px solid #333;margin:22px 0}
  #msg{margin-top:14px;padding:10px;border-radius:8px;font-size:0.88rem;display:none}
  .ok{background:#1a3a1a;color:#6fcf6f;border:1px solid #2d6a2d}
  .err{background:#3a1a1a;color:#cf6f6f;border:1px solid #6a2d2d}
  small{color:#666;font-size:0.75rem;display:block;margin-top:6px}
</style>
</head>
<body>
<div class="card">
  <h1>Ramoncito 🤖</h1>
)rawhtml";

    // Subtítulo según el modo
    html += WIFI_INTENTAR_STA
                ? String("  <p class=\"sub\">Configuración de red y hora</p>\n")
                : String("  <p class=\"sub\">Modo sin WiFi — solo configurar hora</p>\n");

    // Banner de error de la última conexión (si lo hay)
    if (_ultimoError.length() > 0) {
        html += "<div class='banner'>" + _ultimoError + "</div>";
    }

    // Sección WiFi (elegir/guardar red): SOLO en modo STA/NTP. En modo SOLO-AP
    // no hay red que elegir, así que se oculta y queda solo el botón de hora.
    if (WIFI_INTENTAR_STA) {
        html += R"rawhtml(
  <!-- Sección WiFi -->
  <form method="POST" action="/save">
    <label>Redes disponibles (tocá para seleccionar)</label>
    <ul id="redes">)rawhtml";

        // Caché del scan async (o placeholder mientras se busca)
        html += (_scanCache.length() > 0)
                    ? _scanCache
                    : String("<li><em>buscando redes…</em></li>");

        html += R"rawhtml(</ul>
    <button type="button" class="btn btn-secondary btn-mini" onclick="rescan()">Buscar redes de nuevo</button>
    <label for="ssid">Nombre de red (SSID)</label>
    <input type="text" id="ssid" name="ssid" placeholder="Mi Red WiFi" autocomplete="off">
    <label for="pass">Contraseña</label>
    <input type="password" id="pass" name="pass" placeholder="contraseña" autocomplete="off">
    <small>Las credenciales se guardan en la memoria interna del dispositivo.</small>
    <button type="submit" class="btn btn-primary">Guardar y conectar</button>
  </form>

  <hr class="divider">
)rawhtml";
    }

    html += R"rawhtml(
  <!-- Sección hora del teléfono -->
  <p style="font-size:0.85rem;color:#aaa;margin-bottom:12px">
    Poné en hora tu Ramoncito directamente desde este teléfono.
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
function rescan() {
  // /scan dispara un scan async y devuelve la cache actual;
  // repreguntamos a los 4 s para mostrar los resultados frescos.
  var ul = document.getElementById('redes');
  fetch('/scan').then(function(r){ return r.text(); }).then(function(t){
    ul.innerHTML = t;
    setTimeout(function(){
      fetch('/scan').then(function(r){ return r.text(); }).then(function(t2){
        ul.innerHTML = t2;
      });
    }, 4000);
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
    // En la LAN (portal cerrado, server sobre la STA) la raíz muestra el panel;
    // así se entra directo a http://ramoncito.local sin recordar /panel.
    if (!_portalActivo && _lanServer) {
        _handlePanel();
        return;
    }

    // Pedido explícito del usuario (abrió la página): si no hay caché de
    // redes, intentar un scan. _lanzarScan() ya garantiza no molestar si
    // hubo un begin() hace <20 s o hay un scan corriendo.
    if (_scanCache.length() == 0) _lanzarScan();

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
    prefs.begin("ramoncito", false);  // lectura/escritura
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
        "<body><div class='card'><h2>Ramoncito 🤖</h2>"
        "<p>Credenciales guardadas.<br>El dispositivo se reiniciará y conectará a<br>"
        "<strong>" + nuevoSsid + "</strong></p></div></body></html>");

    // Breve pausa para que la respuesta llegue al navegador, luego reiniciar.
    // (Único delay del módulo: termina en reset, no afecta al loop.)
    delay(1500);
    ESP.restart();
}

void Net::_handleSetTime() {
    // Parámetros: epoch (segundos UTC desde 1970) y tzmin (offset zona del cliente)
    // Nota: usamos el epoch UTC del navegador directamente.
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
    // Cuenta como sincronización: habilita el resync NTP diario en REPOSO
    _tUltimaSync = millis();

    // El portal cumplió su función: programar cierre DIFERIDO (lo ejecuta
    // update(); prohibido parar el server desde este handler).
    _cierrePendiente = true;
    _tCierrePortal   = millis() + CIERRE_PORTAL_MS;
    Serial.println("[net] cierre del portal programado en 3 s");

    String respuesta = "Hora configurada: ";
    char buf[40];
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d (hora local).",
             ti.tm_hour, ti.tm_min, ti.tm_sec);
    respuesta += buf;
    respuesta += " El portal se cierra en unos segundos. ¡Listo!";

    _server->send(200, "text/plain; charset=utf-8", respuesta);
}

void Net::_handleScan() {
    // Disparar un rescan ASINCRÓNICO (si se puede) y responder la caché
    // actual; el navegador repregunta a los segundos para ver lo nuevo.
    _lanzarScan();
    String frag = (_scanCache.length() > 0)
                      ? _scanCache
                      : String("<li><em>buscando redes…</em></li>");
    _server->send(200, "text/html; charset=utf-8", frag);
}

void Net::_handleCaptiveRedirect() {
    // Responder a las sondas de portal cautivo de Android/iOS/Windows
    // con una redirección a la raíz para que el sistema operativo
    // detecte el portal y muestre la notificación.
    _server->sendHeader("Location", "http://" + AP_IP.toString() + "/", true);
    _server->send(302, "text/plain", "");
}

// ================================================================
//  _handleOtaGet() — sirve el formulario de subida de firmware
// ================================================================
void Net::_handleOtaGet() {
    if (!_server->authenticate(OTA_USUARIO, OTA_CLAVE)) {
        _server->requestAuthentication();
        return;
    }

    String html =
        "<!DOCTYPE html><html lang='es'>"
        "<head>"
        "<meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Ramoncito \xe2\x80\x94 Actualizar firmware</title>"
        "<style>"
        "*{box-sizing:border-box;margin:0;padding:0}"
        "body{background:#121212;color:#e0e0e0;font-family:sans-serif;"
        "display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px}"
        ".card{background:#1e1e1e;border-radius:16px;padding:28px 24px;"
        "max-width:420px;width:100%;box-shadow:0 8px 32px rgba(0,0,0,0.6)}"
        "h1{font-size:1.4rem;font-weight:700;margin-bottom:6px;text-align:center}"
        ".ver{color:#888;font-size:0.82rem;text-align:center;margin-bottom:22px}"
        "label{display:block;font-size:0.85rem;color:#aaa;margin-bottom:8px}"
        "input[type=file]{width:100%;background:#2a2a2a;border:1px solid #444;"
        "border-radius:8px;color:#ccc;padding:10px 12px;font-size:0.9rem}"
        ".btn{display:block;width:100%;margin-top:18px;padding:14px;border:none;"
        "border-radius:10px;font-size:1.05rem;font-weight:600;cursor:pointer;"
        "background:#5b9cf6;color:#000}"
        ".btn:active{background:#3a7fd4}"
        "</style>"
        "</head>"
        "<body><div class='card'>"
        "<h1>Ramoncito &#x1F916; Firmware</h1>"
        "<p class='ver'>Versi\xc3\xb3n actual: " FW_VERSION "</p>"
        "<form method='POST' action='/update' enctype='multipart/form-data'>"
        "<label for='fw'>Seleccion\xc3\xa1 el archivo <strong>firmware.bin</strong>:</label>"
        "<input type='file' id='fw' name='firmware' accept='.bin'>"
        "<button type='submit' class='btn'>Actualizar</button>"
        "</form>"
        "</div></body></html>";

    _server->send(200, "text/html; charset=utf-8", html);
}

// ================================================================
//  _handleOtaPost() — responde al cliente con el resultado
// ================================================================
void Net::_handleOtaPost() {
    if (!_server->authenticate(OTA_USUARIO, OTA_CLAVE)) {
        _server->requestAuthentication();
        return;
    }

    bool ok = !Update.hasError();

    String html =
        "<!DOCTYPE html><html lang='es'>"
        "<head><meta charset='UTF-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<title>Ramoncito \xe2\x80\x94 OTA</title>"
        "<style>"
        "body{background:#121212;color:#e0e0e0;font-family:sans-serif;"
        "display:flex;justify-content:center;align-items:center;min-height:100vh;padding:16px}"
        ".card{background:#1e1e1e;border-radius:16px;padding:28px 24px;"
        "max-width:420px;width:100%;text-align:center}"
        "h2{margin-bottom:12px}"
        "p{color:#aaa;font-size:0.9rem}"
        ".ok{color:#6fcf6f}"
        ".err{color:#cf6f6f}"
        "</style></head><body><div class='card'>";

    if (ok) {
        html += "<h2 class='ok'>&#10003; Actualizaci\xc3\xb3n exitosa</h2>"
                "<p>El dispositivo se est\xc3\xa1 reiniciando...</p>";
    } else {
        html += "<h2 class='err'>&#10007; Error al actualizar</h2>"
                "<p>Revis\xc3\xa1 el archivo y vuelv\xc3\xa1 a intentarlo.</p>"
                "<p style='margin-top:14px'>"
                "<a href='/update' style='color:#5b9cf6'>Volver</a></p>";
    }

    html += "</div></body></html>";

    // Enviar respuesta antes de reiniciar (el keep-alive no importa aquí)
    _server->sendHeader("Connection", "close");
    _server->send(ok ? 200 : 500, "text/html; charset=utf-8", html);

    if (ok) {
        Serial.println("[ota] actualización correcta — reiniciando en 500 ms");
        delay(500);
        ESP.restart();
    }
}

// ================================================================
//  _handleOtaUpload() — recibe el .bin en fragmentos y lo escribe
// ================================================================
void Net::_handleOtaUpload() {
    HTTPUpload& upload = _server->upload();

    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("[ota] inicio de subida: %s\n", upload.filename.c_str());
        // UPDATE_SIZE_UNKNOWN: Update calcula el espacio necesario en tiempo real
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
            Update.printError(Serial);
        }

    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }

    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) {   // true = fijar el boot partition
            Serial.printf("[ota] subida completa: %u bytes\n", upload.totalSize);
        } else {
            Update.printError(Serial);
        }
    }
}

// ================================================================
//  lanIP() — IP del toy en la LAN ("" si no conectado)
// ================================================================
String Net::lanIP() const {
    if (WiFi.status() == WL_CONNECTED) return WiFi.localIP().toString();
    return String();
}

// ================================================================
//  Panel web en la LAN
// ================================================================

// Basic Auth con OTA_USUARIO/OTA_CLAVE. Devuelve false (y ya pidió login)
// si no está autorizado: el handler debe retornar de inmediato.
bool Net::_panelAutorizado() {
    if (!_server->authenticate(OTA_USUARIO, OTA_CLAVE)) {
        _server->requestAuthentication();
        return false;
    }
    return true;
}

// GET /panel — dashboard HTML (los datos los trae luego /api/state por fetch)
void Net::_handlePanel() {
    if (!_panelAutorizado()) return;
    _server->send(200, "text/html; charset=utf-8", _htmlPanel());
}

// GET /api/state — snapshot del estado en JSON (lo refresca main.cpp)
void Net::_handleApiState() {
    if (!_panelAutorizado()) return;
    char buf[512];
    snprintf(buf, sizeof(buf),
        "{\"felicidad\":%u,\"energia\":%u,\"aburrimiento\":%u,"
        "\"animo\":%u,\"energiaPers\":%u,\"edadDias\":%d,"
        "\"sonido\":%s,\"hayUpdate\":%s,\"fw\":\"%s\",\"verNueva\":\"%s\","
        "\"expr\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\"}",
        _web.felicidad, _web.energia, _web.aburrimiento,
        _web.animo, _web.energia_pers, _web.edadDias,
        _web.sonido ? "true" : "false",
        _web.hayUpdate ? "true" : "false",
        _web.fwVersion, _web.versionNueva, _web.expresion,
        _ssid.c_str(), WiFi.localIP().toString().c_str());
    _server->send(200, "application/json; charset=utf-8", buf);
}

// GET /api/action?do=... — encola una acción; main.cpp la ejecuta en su loop.
// Nunca ejecuta aquí (el handler no debe bloquear ni reiniciar en medio).
void Net::_handleApiAction() {
    if (!_panelAutorizado()) return;
    String d = _server->arg("do");
    const char* msg = "ok";
    int code = 200;

    if (d == "sonido") {
        _accionWeb = WebAction::TOGGLE_SONIDO; msg = "sonido cambiado";
    } else if (d == "ota_check") {
        _accionWeb = WebAction::OTA_CHECK;     msg = "chequeando actualizacion...";
    } else if (d == "ota_install") {
        if (!_web.hayUpdate) { code = 409; msg = "no hay actualizacion disponible"; }
        else { _accionWeb = WebAction::OTA_INSTALL; msg = "instalando... el toy se reinicia"; }
    } else if (d == "portal") {
        _accionWeb = WebAction::ABRIR_PORTAL;  msg = "abri la red Ramoncito-setup en el toy";
    } else if (d == "renacer") {
        if (_server->arg("confirm") != "1") { code = 400; msg = "falta confirmacion"; }
        else { _accionWeb = WebAction::RENACER; msg = "renaciendo... borra todo"; }
    } else {
        code = 400; msg = "accion desconocida";
    }

    char buf[160];
    snprintf(buf, sizeof(buf), "{\"ok\":%s,\"msg\":\"%s\"}",
             code == 200 ? "true" : "false", msg);
    _server->send(code, "application/json; charset=utf-8", buf);
}

// HTML del dashboard: estático; se hidrata y auto-refresca vía /api/state.
String Net::_htmlPanel() {
    return String(R"rawhtml(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#141026">
<title>Ramoncito</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
  :root{--card:rgba(255,255,255,.055);--line:rgba(255,255,255,.10);
        --txt:#f2ede4;--dim:#a99fb5;--acc:#ffb454;--pk:#ff7eb6}
  body{min-height:100vh;color:var(--txt);font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif;
       padding:22px 16px 46px;display:flex;justify-content:center;
       background:radial-gradient(120% 80% at 12% -8%,#2a2350 0%,transparent 55%),
                  radial-gradient(120% 80% at 95% 5%,#4a2340 0%,transparent 52%),#141026}
  /* ---- Layout responsivo: teléfono = 1 columna; desktop = 2 (horizontal) ---- */
  .app{width:100%;max-width:460px;display:flex;flex-direction:column;gap:16px}
  @media(min-width:840px){
    .app{max-width:940px;display:grid;grid-template-columns:minmax(360px,1fr) minmax(340px,420px);
         align-items:start;gap:26px}
    .stage{position:sticky;top:22px}
    .panels{display:flex;flex-direction:column;gap:16px}
  }
  .panels{display:flex;flex-direction:column;gap:16px}

  .head{text-align:center;margin-bottom:2px}
  .name{font-size:1.7rem;font-weight:800;letter-spacing:.01em}
  .net{font-size:.74rem;color:var(--dim);margin-top:2px}
  .net b{color:var(--acc);font-weight:700}

  /* ---- Escenario del personaje ---- */
  .stage{background:linear-gradient(180deg,rgba(255,255,255,.06),rgba(255,255,255,.02));
    border:1px solid var(--line);border-radius:26px;padding:18px 18px 14px;text-align:center;
    box-shadow:0 16px 44px rgba(0,0,0,.4)}
  .crt{width:100%;max-width:340px;margin:0 auto;cursor:pointer;display:block;overflow:visible}
  .crt .body,.crt .arm,.crt .foot,.crt .horn{transition:transform .3s}
  #creature{transform-origin:150px 200px;animation:breathe 4.6s ease-in-out infinite}
  @keyframes breathe{0%,100%{transform:translateY(0) scale(1)}50%{transform:translateY(-3px) scale(1.012)}}
  .bounce #creature{animation:bounce .5s ease-in-out infinite}
  @keyframes bounce{0%,100%{transform:translateY(0)}50%{transform:translateY(-10px)}}
  .pet{animation:squish .45s ease}
  @keyframes squish{0%,100%{transform:scaleY(1) scaleX(1)}40%{transform:scaleY(.9) scaleX(1.08)}}
  .pupil{transition:transform .5s cubic-bezier(.3,1.2,.5,1)}
  .lid{transition:transform .12s ease}
  .brow{transition:transform .3s,opacity .3s}
  .mouth path,.mouth ellipse{transition:d .3s,opacity .3s}
  .mood{font-size:1rem;font-weight:700;color:var(--acc);margin-top:4px;min-height:1.3em}
  .hint{font-size:.72rem;color:var(--dim);margin-top:1px}

  /* ---- Cards ---- */
  .card{background:var(--card);border:1px solid var(--line);border-radius:20px;padding:17px 17px 19px;
    box-shadow:0 10px 30px rgba(0,0,0,.28)}
  .card h2{font-size:.68rem;text-transform:uppercase;letter-spacing:.12em;color:var(--dim);margin-bottom:14px}
  .stat{margin-bottom:14px}
  .stat:last-child{margin-bottom:0}
  .slbl{display:flex;justify-content:space-between;align-items:center;font-size:.86rem;margin-bottom:6px}
  .slbl .ic{margin-right:6px}
  .chip{font-variant-numeric:tabular-nums;font-weight:700;font-size:.8rem;
    background:rgba(255,255,255,.09);padding:1px 9px;border-radius:20px}
  .track{height:11px;background:rgba(255,255,255,.08);border-radius:20px;overflow:hidden}
  .fill{height:100%;border-radius:20px;width:0;transition:width .7s cubic-bezier(.4,1.3,.5,1)}
  .f-fel{background:linear-gradient(90deg,#ffd76b,#ff934c)}
  .f-ene{background:linear-gradient(90deg,#7cf2a6,#25c26b)}
  .f-abu{background:linear-gradient(90deg,#c9a0ff,#7a5cff)}

  .plot{position:relative;width:100%;max-width:250px;aspect-ratio:1/1;margin:2px auto 6px;border-radius:16px;
    background:linear-gradient(90deg,rgba(255,120,120,.13),transparent 45%,transparent 55%,rgba(120,220,150,.13)),
      linear-gradient(0deg,rgba(120,150,255,.05),rgba(255,220,120,.10));border:1px solid var(--line)}
  .plot .grid{position:absolute;inset:0;border-radius:16px;
    background:linear-gradient(var(--line) 1px,transparent 1px) 0 50%/100% 50%,
               linear-gradient(90deg,var(--line) 1px,transparent 1px) 50% 0/50% 100%}
  .dot{position:absolute;width:20px;height:20px;border-radius:50%;background:var(--acc);
    box-shadow:0 0 16px rgba(255,180,84,.9),0 0 4px #fff inset;transform:translate(-50%,50%);transition:left .6s,bottom .6s}
  .cnr{position:absolute;font-size:.66rem;color:var(--dim);font-weight:600}
  .cnr.t{top:6px;left:50%;transform:translateX(-50%)}.cnr.b{bottom:6px;left:50%;transform:translateX(-50%)}
  .cnr.l{left:7px;top:50%;transform:translateY(-50%) rotate(-90deg)}
  .cnr.r{right:7px;top:50%;transform:translateY(-50%) rotate(90deg)}
  .meta{display:flex;justify-content:space-between;font-size:.85rem;margin-top:8px}
  .meta span:first-child{color:var(--dim)}
  .upd{background:linear-gradient(135deg,rgba(40,194,107,.22),rgba(40,194,107,.08));color:#8bf0b4;
    border:1px solid rgba(40,194,107,.4);border-radius:12px;padding:10px 12px;font-size:.84rem;margin:2px 0 4px;display:none}

  .btn{display:flex;align-items:center;justify-content:center;gap:8px;width:100%;margin-top:10px;padding:13px;
    border:1px solid var(--line);border-radius:13px;font-size:.96rem;font-weight:600;cursor:pointer;
    background:rgba(255,255,255,.06);color:var(--txt);transition:transform .06s,background .2s}
  .btn:active{transform:scale(.97);background:rgba(255,255,255,.12)}
  .btn.primary{background:linear-gradient(135deg,#ffb454,#ff8f4c);color:#2a1400;border:none}
  .btn.danger{background:rgba(255,90,90,.1);color:#ff8a8a;border-color:rgba(255,90,90,.32)}

  #toast{position:fixed;left:50%;bottom:24px;transform:translateX(-50%) translateY(10px);
    background:rgba(30,22,48,.97);color:#fff;padding:11px 18px;border-radius:12px;font-size:.86rem;
    border:1px solid var(--line);box-shadow:0 10px 30px rgba(0,0,0,.6);opacity:0;transition:all .3s;pointer-events:none}
  #toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
</style>
</head>
<body>
<div class="app">
  <div class="stage">
    <div class="head">
      <div class="name">Ramoncito</div>
      <div class="net" id="net">conectando&hellip;</div>
    </div>
    <svg class="crt" id="crt" viewBox="0 0 300 300" onclick="petear()">
      <defs>
        <filter id="fur" x="-25%" y="-25%" width="150%" height="150%">
          <feTurbulence type="fractalNoise" baseFrequency="0.14 0.16" numOctaves="2" seed="6" result="n"/>
          <feDisplacementMap in="SourceGraphic" in2="n" scale="15" xChannelSelector="R" yChannelSelector="G"/>
        </filter>
        <radialGradient id="gbody" cx="42%" cy="32%" r="78%">
          <stop offset="0%" stop-color="#8f7bd6"/><stop offset="55%" stop-color="#6f5ac0"/>
          <stop offset="100%" stop-color="#4f3d97"/>
        </radialGradient>
        <radialGradient id="gbelly" cx="50%" cy="40%" r="70%">
          <stop offset="0%" stop-color="#fbeede"/><stop offset="100%" stop-color="#e9d3b8"/>
        </radialGradient>
      </defs>
      <g id="creature">
        <ellipse class="shadow" cx="150" cy="272" rx="86" ry="16" fill="rgba(0,0,0,.28)"/>
        <!-- pies -->
        <ellipse class="foot" cx="112" cy="262" rx="26" ry="17" fill="#5b47a6"/>
        <ellipse class="foot" cx="188" cy="262" rx="26" ry="17" fill="#5b47a6"/>
        <!-- brazos -->
        <ellipse class="arm" cx="56" cy="196" rx="20" ry="30" fill="#634fb0" filter="url(#fur)"/>
        <ellipse class="arm" cx="244" cy="196" rx="20" ry="30" fill="#634fb0" filter="url(#fur)"/>
        <!-- orejas/cuernitos peludos -->
        <path class="horn" d="M96 78 Q88 30 118 58 Z" fill="#6f5ac0" filter="url(#fur)"/>
        <path class="horn" d="M204 78 Q212 30 182 58 Z" fill="#6f5ac0" filter="url(#fur)"/>
        <!-- cuerpo peludo -->
        <path class="body" filter="url(#fur)" fill="url(#gbody)"
          d="M150 56 C104 56 66 96 66 156 C66 226 100 262 150 262 C200 262 234 226 234 156 C234 96 196 56 150 56 Z"/>
        <!-- panza -->
        <ellipse cx="150" cy="188" rx="52" ry="58" fill="url(#gbelly)" opacity=".95"/>
        <!-- mejillas -->
        <ellipse class="cheek" cx="96" cy="176" rx="15" ry="10" fill="#ff9ec4" opacity=".55"/>
        <ellipse class="cheek" cx="204" cy="176" rx="15" ry="10" fill="#ff9ec4" opacity=".55"/>
        <!-- cejas -->
        <g class="brows">
          <rect class="brow bl" x="86" y="104" width="42" height="9" rx="4" fill="#3f3080" opacity="0"/>
          <rect class="brow br" x="172" y="104" width="42" height="9" rx="4" fill="#3f3080" opacity="0"/>
        </g>
        <!-- ojos -->
        <g class="eyes">
          <g class="eye" transform="translate(108 140)">
            <ellipse class="white" cx="0" cy="0" rx="26" ry="28" fill="#fffdf7"/>
            <circle class="pupil pl" cx="0" cy="2" r="12" fill="#241a3a"/>
            <circle cx="-4" cy="-3" r="4" fill="#fff"/>
            <rect class="lid ll" x="-30" y="-34" width="60" height="34" rx="6" fill="#6f5ac0" transform="translate(0 -34)"/>
          </g>
          <g class="eye" transform="translate(192 140)">
            <ellipse class="white" cx="0" cy="0" rx="26" ry="28" fill="#fffdf7"/>
            <circle class="pupil pr" cx="0" cy="2" r="12" fill="#241a3a"/>
            <circle cx="-4" cy="-3" r="4" fill="#fff"/>
            <rect class="lid lr" x="-30" y="-34" width="60" height="34" rx="6" fill="#6f5ac0" transform="translate(0 -34)"/>
          </g>
        </g>
        <!-- corazones (enamorado) -->
        <g class="hearts" opacity="0">
          <text x="108" y="152" font-size="34" text-anchor="middle" fill="#ff7eb6">&#10084;</text>
          <text x="192" y="152" font-size="34" text-anchor="middle" fill="#ff7eb6">&#10084;</text>
        </g>
        <!-- boca -->
        <g class="mouth" fill="none" stroke="#3f3080" stroke-width="6" stroke-linecap="round">
          <path class="m-line" d="M132 206 Q150 218 168 206"/>
          <ellipse class="m-open" cx="150" cy="210" rx="15" ry="13" fill="#7a2f4a" stroke="none" opacity="0"/>
        </g>
        <!-- Zzz (dormido) -->
        <g class="zzz" opacity="0"><text x="228" y="120" font-size="26" fill="#cdbff5" font-weight="800">z&#8202;Z</text></g>
      </g>
    </svg>
    <div class="mood" id="mood">&nbsp;</div>
    <div class="hint">toc&aacute;me &#128075;</div>
  </div>

  <div class="panels">
    <div class="card">
      <h2>Estado de &aacute;nimo</h2>
      <div class="stat"><div class="slbl"><span><span class="ic">&#9728;&#65039;</span>Felicidad</span><span class="chip" id="v-fel">--</span></div>
        <div class="track"><div class="fill f-fel" id="b-fel"></div></div></div>
      <div class="stat"><div class="slbl"><span><span class="ic">&#9889;</span>Energ&iacute;a</span><span class="chip" id="v-ene">--</span></div>
        <div class="track"><div class="fill f-ene" id="b-ene"></div></div></div>
      <div class="stat"><div class="slbl"><span><span class="ic">&#128564;</span>Aburrimiento</span><span class="chip" id="v-abu">--</span></div>
        <div class="track"><div class="fill f-abu" id="b-abu"></div></div></div>
    </div>

    <div class="card">
      <h2>Personalidad</h2>
      <div class="plot">
        <div class="grid"></div>
        <span class="cnr t">en&eacute;rgico</span><span class="cnr b">perezoso</span>
        <span class="cnr l">gru&ntilde;&oacute;n</span><span class="cnr r">alegre</span>
        <div class="dot" id="dot" style="left:50%;bottom:50%"></div>
      </div>
      <div class="meta"><span>Edad</span><span id="v-edad">--</span></div>
    </div>

    <div class="card">
      <h2>Firmware</h2>
      <div class="meta"><span>Versi&oacute;n</span><span id="v-fw">--</span></div>
      <div class="upd" id="upd"></div>
      <button class="btn" onclick="act('ota_check')"><span>&#128260;</span>Buscar actualizaci&oacute;n</button>
      <button class="btn primary" id="btn-inst" style="display:none" onclick="act('ota_install','Instalar la nueva versión? El toy se reinicia.')"><span>&#11015;&#65039;</span>Instalar actualizaci&oacute;n</button>
    </div>

    <div class="card">
      <h2>Ajustes</h2>
      <button class="btn" id="btn-snd" onclick="act('sonido')"><span>&#128266;</span>Sonido</button>
      <button class="btn" onclick="act('portal','Abrir el portal de WiFi en el toy?')"><span>&#128246;</span>Cambiar WiFi</button>
      <button class="btn danger" onclick="act('renacer','RENACER borra TODO (personalidad, humor, edad). Seguro?')"><span>&#128293;</span>Renacer</button>
    </div>
  </div>
</div>
<div id="toast"></div>

<script>
var MOODS={tranquilo:"Tranquilo",neutral:"Tranquilo",feliz:"Feliz",riendo:"Se ríe",triste:"Triste",
  enojado:"Enojado",sorprendido:"Sorprendido",aburrido:"Aburrido",dormido:"Durmiendo",
  sospechoso:"Desconfiado",enamorado:"Enamorado",mareado:"Mareado",ilusionado:"Ilusionado"};
var C=document.getElementById('crt'), CR=document.getElementById('creature');
var pl=C.querySelector('.pl'), pr=C.querySelector('.pr');
var ll=C.querySelector('.ll'), lr=C.querySelector('.lr');
var bl=C.querySelector('.bl'), br=C.querySelector('.br');
var mline=C.querySelector('.m-line'), mopen=C.querySelector('.m-open');
var hearts=C.querySelector('.hearts'), zzz=C.querySelector('.zzz'), cheeks=C.querySelectorAll('.cheek');
var expr='tranquilo', petting=false;

function eyesOpen(o){ // o=0 abierto .. 1 cerrado
  var y=-34+o*34; ll.setAttribute('transform','translate(0 '+y+')'); lr.setAttribute('transform','translate(0 '+y+')');
}
function look(dx,dy){
  pl.style.transform='translate('+dx+'px,'+dy+'px)';
  pr.style.transform='translate('+dx+'px,'+dy+'px)';
}
function setMouth(kind){ // 'smile','big','frown','flat','o'
  mopen.style.opacity=(kind==='big'||kind==='o')?1:0;
  var d={smile:'M132 206 Q150 220 168 206',frown:'M132 214 Q150 200 168 214',
         flat:'M134 210 L166 210',big:'M132 206 Q150 208 168 206',o:'M140 208 Q150 210 160 208'}[kind]||'M132 206 Q150 216 168 206';
  mline.setAttribute('d',d);
  if(kind==='o'){mopen.setAttribute('rx',9);mopen.setAttribute('ry',9);}else{mopen.setAttribute('rx',15);mopen.setAttribute('ry',13);}
}
function brows(on,ang){
  bl.style.opacity=on?1:0; br.style.opacity=on?1:0;
  bl.style.transform=on?('rotate('+ang+'deg)'):''; br.style.transform=on?('rotate('+(-ang)+'deg)'):'';
  bl.style.transformOrigin='107px 108px'; br.style.transformOrigin='193px 108px';
}
// aplica la expresión al personaje
function applyExpr(e){
  expr=e; CR.parentElement.classList.remove('bounce');
  hearts.style.opacity=0; zzz.style.opacity=0; brows(false,0); eyesOpen(0);
  cheeks.forEach(function(c){c.style.opacity=.55;});
  var pf=C.querySelectorAll('.pupil'); pf.forEach(function(p){p.style.opacity=1;});
  if(e==='feliz'){setMouth('smile');cheeks.forEach(function(c){c.style.opacity=.85;});}
  else if(e==='riendo'){setMouth('big');CR.parentElement.classList.add('bounce');cheeks.forEach(function(c){c.style.opacity=.85;});}
  else if(e==='triste'){setMouth('frown');look(0,4);}
  else if(e==='enojado'){setMouth('frown');brows(true,18);}
  else if(e==='sorprendido'){setMouth('o');}
  else if(e==='aburrido'){setMouth('flat');eyesOpen(.5);}
  else if(e==='dormido'){setMouth('flat');eyesOpen(1);zzz.style.opacity=1;}
  else if(e==='sospechoso'){setMouth('flat');eyesOpen(.35);look(6,0);}
  else if(e==='enamorado'){setMouth('smile');hearts.style.opacity=1;pf.forEach(function(p){p.style.opacity=0;});cheeks.forEach(function(c){c.style.opacity=.9;});}
  else if(e==='mareado'){setMouth('o');}
  else if(e==='ilusionado'){setMouth('big');}
  else {setMouth('smile');} // tranquilo/neutral
}
// vida en reposo: parpadeos y mirada errante (salvo dormido/enamorado)
function alive(){
  var quiet=(expr!=='dormido'&&expr!=='enamorado'&&expr!=='mareado');
  if(expr==='dormido'){ // duerme pero cada tanto espía un ojo
    if(Math.random()<.25){eyesOpen(.6);setTimeout(function(){if(expr==='dormido')eyesOpen(1);},700);}
  } else if(quiet){
    eyesOpen(1);setTimeout(function(){eyesOpen(expr==='aburrido'?.5:(expr==='sospechoso'?.35:0));},130); // parpadeo
    if(Math.random()<.6){var dx=(Math.random()*16-8),dy=(Math.random()*8-3);look(dx,dy);
      setTimeout(function(){if(!petting)look(0,expr==='triste'?4:0);},1300);}
  }
  setTimeout(alive, 2200+Math.random()*3200);
}
// reacción al tocarlo/click
function petear(){
  petting=true; CR.classList.remove('pet'); void CR.offsetWidth; CR.classList.add('pet');
  var prev=expr; applyExpr('enamorado'); toast('🥰');
  setTimeout(function(){petting=false;applyExpr(prev);},1400);
}

function pct(x){return Math.max(0,Math.min(100,x))+'%';}
function toast(m){var t=document.getElementById('toast');t.textContent=m;t.classList.add('show');
  clearTimeout(t._h);t._h=setTimeout(function(){t.classList.remove('show');},2600);}
async function refresh(){
  try{
    var r=await fetch('/api/state');if(!r.ok)return;var s=await r.json();
    document.getElementById('net').innerHTML=(s.ssid?(s.ssid+' &middot; '):'')+'<b>'+s.ip+'</b>';
    var e=(s.expr||'tranquilo');
    if(!petting && e!==expr) applyExpr(e);
    document.getElementById('mood').textContent=MOODS[e]||'Tranquilo';
    document.getElementById('v-fel').textContent=s.felicidad;
    document.getElementById('v-ene').textContent=s.energia;
    document.getElementById('v-abu').textContent=s.aburrimiento;
    document.getElementById('b-fel').style.width=pct(s.felicidad);
    document.getElementById('b-ene').style.width=pct(s.energia);
    document.getElementById('b-abu').style.width=pct(s.aburrimiento);
    var dot=document.getElementById('dot');dot.style.left=pct(s.animo);dot.style.bottom=pct(s.energiaPers);
    document.getElementById('v-edad').textContent=(s.edadDias<0?'recién nacido':s.edadDias+' días');
    document.getElementById('v-fw').textContent='v'+s.fw;
    document.getElementById('btn-snd').innerHTML='<span>'+(s.sonido?'🔊':'🔇')+'</span>Sonido: '+(s.sonido?'ON':'OFF');
    var upd=document.getElementById('upd'),inst=document.getElementById('btn-inst');
    if(s.hayUpdate){upd.style.display='block';upd.textContent='✨ Nueva versión '+s.verNueva+' disponible';inst.style.display='flex';}
    else{upd.style.display='none';inst.style.display='none';}
  }catch(e){}
}
async function act(doName,confirmMsg){
  if(confirmMsg&&!confirm(confirmMsg))return;
  var q='/api/action?do='+doName+(doName==='renacer'?'&confirm=1':'');
  try{var r=await fetch(q);var j=await r.json();toast(j.msg);}catch(e){toast('error de red');}
  setTimeout(refresh,900);
}
applyExpr('tranquilo'); alive(); setInterval(refresh,2000); refresh();
</script>
</body>
</html>)rawhtml");
}
