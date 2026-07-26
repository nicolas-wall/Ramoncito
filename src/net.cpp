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
#include "notify.h"

// Clave opcional del webhook desde secrets.h (si existe).
#if __has_include("secrets.h")
  #include "secrets.h"
#endif
#ifndef NOTIFY_TOKEN
  #define NOTIFY_TOKEN ""
#endif

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
    // Webhook de notificaciones: cualquier app de la LAN empuja un aviso.
    if (NOTIFY_HABILITADO) {
        _server->on("/api/notify",  [this]() { _handleApiNotify(); });
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

// GET/POST /api/notify — empuja una notificación a la pantalla del toy.
// Params: titulo|title, texto|text, icono|icon (chat|mail|bell|alerta|reloj),
//         token (opcional). Auth: Basic Auth del panel, O ?token=NOTIFY_TOKEN
//         si NOTIFY_TOKEN está configurado (más cómodo para Home Assistant/IFTTT).
//   Ej: curl "http://ramoncito.local/api/notify?title=Mail&text=Tenes+2+nuevos&icon=mail" -u ramoncito:ramoncito
void Net::_handleApiNotify() {
    // Autorización: token del webhook o, si no, Basic Auth.
    bool tokOk = (strlen(NOTIFY_TOKEN) > 0 && _server->arg("token") == NOTIFY_TOKEN);
    if (!tokOk && !_panelAutorizado()) return;   // _panelAutorizado() ya pidió login

    String titulo = _server->arg("titulo"); if (!titulo.length()) titulo = _server->arg("title");
    String texto  = _server->arg("texto");  if (!texto.length())  texto  = _server->arg("text");
    String icono  = _server->arg("icono");  if (!icono.length())  icono  = _server->arg("icon");

    if (!texto.length() && !titulo.length()) {
        _server->send(400, "application/json; charset=utf-8",
                      "{\"ok\":false,\"msg\":\"falta title/text\"}");
        return;
    }
    if (!titulo.length()) titulo = "Aviso";

    notify.push(titulo.c_str(), texto.c_str(), Notify::iconoDeTexto(icono.c_str()));
    Serial.printf("[web] notify: %s\n", titulo.c_str());
    _server->send(200, "application/json; charset=utf-8", "{\"ok\":true}");
}

// HTML del dashboard: estilo "character creator" dibujado a mano (cutekart).
String Net::_htmlPanel() {
    return String(R"rawhtml(<!DOCTYPE html>
<html lang="es">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<meta name="theme-color" content="#e9dcc0">
<title>Ramoncito</title>
<style>
  *{box-sizing:border-box;margin:0;padding:0;-webkit-tap-highlight-color:transparent}
  :root{--paper:#f5ecd6;--paper2:#efe3c6;--ink:#2b2620;--blue:#7d97c4;--orange:#e2a24c;--red:#cf5a41}
  body{min-height:100vh;display:flex;justify-content:center;align-items:flex-start;padding:22px 14px 44px;
    font-family:"Comic Sans MS","Comic Sans","Chalkboard SE","Marker Felt","Segoe Print",cursive;color:var(--ink);
    background:#cfc7b6;background-image:radial-gradient(circle at 28% 18%,#dad3c3,transparent 60%),
      radial-gradient(circle at 82% 72%,#c5bca9,transparent 55%)}
  .win{width:100%;max-width:540px;background:var(--paper);
    border:3px solid var(--ink);border-radius:14px 10px 16px 9px/9px 16px 10px 14px;
    box-shadow:6px 8px 0 rgba(0,0,0,.18);overflow:hidden}
  .tbar{display:flex;align-items:center;gap:8px;border-bottom:3px solid var(--ink);padding:7px 10px;background:var(--paper2)}
  .url{flex:1;font-size:.72rem;font-weight:700;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  .wbtn{width:22px;height:20px;border:2.5px solid var(--ink);border-radius:6px 4px 6px 4px/4px 6px 4px 6px;
    display:flex;align-items:center;justify-content:center;font-size:.8rem;font-weight:800;background:var(--paper);cursor:pointer}
  .body{display:grid;grid-template-columns:1fr 1fr;gap:14px;padding:14px}
  .col{min-width:0}
  .stat{margin-bottom:11px}
  .slbl{font-size:.8rem;font-weight:800;letter-spacing:.02em;margin-bottom:4px;text-transform:uppercase}
  .seg{display:flex;gap:3px;height:19px}
  .seg i{flex:1;border:2.2px solid var(--ink);border-radius:5px 3px 5px 3px/3px 5px 3px 5px;background:var(--paper)}
  .seg i.on{background:var(--blue)}
  .seg.warn i.on{background:var(--orange)}
  .selcap{font-size:.64rem;font-weight:800;opacity:.6;text-transform:uppercase;margin:9px 0 -2px 2px}
  .sel{display:flex;align-items:center;gap:8px}
  .arw{font-size:1rem;font-weight:900;opacity:.45}
  .selbox{flex:1;text-align:center;border:2.5px solid var(--ink);
    border-radius:16px 10px 18px 10px/10px 18px 10px 16px;padding:6px 8px;font-weight:800;
    font-size:.86rem;text-transform:uppercase;background:var(--paper)}
  .icons{display:flex;gap:9px;margin-top:14px}
  .ico{width:46px;height:40px;border:2.8px solid var(--ink);border-radius:12px 8px 12px 8px/8px 12px 8px 12px;
    background:var(--paper);cursor:pointer;display:flex;align-items:center;justify-content:center;font-size:20px;position:relative;transition:transform .06s}
  .ico:active{transform:translateY(2px)}
  .ico.red{background:#f2c9bf}.ico.warn{background:#f3dcae}
  .ico .bang{position:absolute;top:-7px;right:-6px;background:var(--red);color:#fff;font-size:.6rem;font-weight:900;
    width:16px;height:16px;border-radius:50%;border:2px solid var(--ink);display:none;align-items:center;justify-content:center}
  .frame{border:3px solid var(--ink);border-radius:16px 12px 18px 10px/10px 18px 12px 16px;
    background:repeating-linear-gradient(0deg,#fbf5e4,#fbf5e4 22px,#f2e8ce 22px,#f2e8ce 24px);
    min-height:230px;display:flex;flex-direction:column;align-items:center;justify-content:center;padding:12px;position:relative}
  .moodtag{position:absolute;top:8px;left:0;right:0;text-align:center;font-size:.8rem;font-weight:800;opacity:.75}
  .ph{width:70%;max-width:150px;animation:bob 4.5s ease-in-out infinite}
  @keyframes bob{0%,100%{transform:translateY(0) rotate(-1deg)}50%{transform:translateY(-8px) rotate(1deg)}}
  .phtag{margin-top:8px;font-size:.72rem;font-weight:800;opacity:.6;border:2px dashed var(--ink);border-radius:20px;padding:3px 12px}
  .namebar{display:flex;align-items:center;gap:8px;margin-top:12px}
  .namebox{flex:1;border:2.8px solid var(--ink);border-radius:16px 10px 18px 10px/10px 18px 10px 16px;
    padding:8px 12px;font-weight:900;font-size:1.05rem;letter-spacing:.03em;text-transform:uppercase;background:var(--paper)}
  .reload{width:40px;height:40px;border:2.8px solid var(--ink);border-radius:50%;background:var(--paper);
    cursor:pointer;display:flex;align-items:center;justify-content:center;font-size:18px}
  .sys{font-size:.66rem;font-weight:700;opacity:.6;margin-top:9px;text-align:center}
  .upd{display:none;margin-top:10px;border:2.5px dashed var(--ink);background:#e7f0d8;border-radius:12px;padding:8px 10px;font-size:.76rem;font-weight:700}
  .upd b{cursor:pointer;text-decoration:underline}
  #toast{position:fixed;left:50%;bottom:22px;transform:translateX(-50%) translateY(8px);background:var(--paper);color:var(--ink);
    border:2.8px solid var(--ink);border-radius:12px 8px 14px 8px/8px 14px 8px 12px;padding:9px 16px;font-size:.82rem;font-weight:800;
    box-shadow:4px 5px 0 rgba(0,0,0,.2);opacity:0;transition:all .25s;pointer-events:none}
  #toast.show{opacity:1;transform:translateX(-50%) translateY(0)}
  @media(max-width:560px){.body{grid-template-columns:1fr}}
</style>
</head>
<body>
<div class="win">
  <div class="tbar">
    <span class="url" id="url">WWW.RAMONCITO.LOCAL / CHARACTER</span>
    <span class="wbtn">_</span><span class="wbtn">&#9633;</span><span class="wbtn" onclick="refresh()">&#10005;</span>
  </div>
  <div class="body">
    <div class="col">
      <div class="stat"><div class="slbl">Felicidad</div><div class="seg" id="s-fel"></div></div>
      <div class="stat"><div class="slbl">Energia</div><div class="seg" id="s-ene"></div></div>
      <div class="stat"><div class="slbl">Aburrimiento</div><div class="seg warn" id="s-abu"></div></div>
      <div class="stat"><div class="slbl">Vitalidad</div><div class="seg" id="s-vit"></div></div>
      <div class="selcap">Humor</div>
      <div class="sel"><span class="arw">&#9664;</span><div class="selbox" id="sel-mood">--</div><span class="arw">&#9654;</span></div>
      <div class="selcap">Caracter</div>
      <div class="sel"><span class="arw">&#9664;</span><div class="selbox" id="sel-car">--</div><span class="arw">&#9654;</span></div>
      <div class="icons">
        <div class="ico" id="ico-snd" title="Sonido" onclick="act('sonido')">&#128266;</div>
        <div class="ico" title="Cambiar WiFi" onclick="act('portal','Abrir el portal de WiFi en el toy?')">&#128246;</div>
        <div class="ico warn" title="Buscar actualizacion" onclick="act('ota_check')">&#128260;<span class="bang" id="bang">!</span></div>
        <div class="ico red" title="Renacer" onclick="act('renacer','RENACER borra TODO (personalidad, humor, edad). Seguro?')">&#128293;</div>
      </div>
    </div>
    <div class="col">
      <div class="frame">
        <div class="moodtag" id="mood">&nbsp;</div>
        <svg class="ph" viewBox="0 0 120 150">
          <g fill="none" stroke="#2b2620" stroke-width="3" stroke-linejoin="round" stroke-linecap="round">
            <ellipse cx="42" cy="30" rx="9" ry="20" fill="#f5ecd6"/>
            <ellipse cx="78" cy="30" rx="9" ry="20" fill="#f5ecd6"/>
            <circle cx="60" cy="62" r="34" fill="#f5ecd6"/>
            <circle cx="49" cy="58" r="4" fill="#2b2620"/>
            <circle cx="71" cy="58" r="4" fill="#2b2620"/>
            <path d="M57 70 L63 70 L60 75 Z" fill="#2b2620"/>
            <path d="M40 96 Q60 138 80 96" fill="#f5ecd6"/>
          </g>
        </svg>
        <div class="phtag">personaje proximamente</div>
      </div>
      <div class="namebar">
        <div class="namebox" id="name">RAMONCITO</div>
        <div class="reload" title="Actualizar" onclick="refresh()">&#8635;</div>
      </div>
      <div class="sys" id="sys">--</div>
      <div class="upd" id="upd"></div>
    </div>
  </div>
</div>
<div id="toast"></div>
<script>
var MOODS={tranquilo:"Tranquilo",neutral:"Tranquilo",feliz:"Feliz",riendo:"Se rie",triste:"Triste",
  enojado:"Enojado",sorprendido:"Sorprendido",aburrido:"Aburrido",dormido:"Durmiendo",
  sospechoso:"Desconfiado",enamorado:"Enamorado",mareado:"Mareado",ilusionado:"Ilusionado"};
var N=10;
function seg(id,val){var el=document.getElementById(id);if(!el)return;
  if(!el._b){for(var i=0;i<N;i++)el.appendChild(document.createElement('i'));el._b=1;}
  var k=Math.round(Math.max(0,Math.min(100,val))/100*N),c=el.children;
  for(var i=0;i<N;i++)c[i].className=(i<k?'on':'');}
function carac(v){return v<35?'GRUNON':(v>65?'ALEGRE':'EQUILIBRADO');}
function toast(m){var t=document.getElementById('toast');t.textContent=m;t.classList.add('show');
  clearTimeout(t._h);t._h=setTimeout(function(){t.classList.remove('show');},2600);}
async function refresh(){
  try{
    var r=await fetch('/api/state');if(!r.ok)return;var s=await r.json();
    var e=(s.expr||'tranquilo');
    document.getElementById('mood').textContent=MOODS[e]||'Tranquilo';
    document.getElementById('sel-mood').textContent=(MOODS[e]||'Tranquilo').toUpperCase();
    document.getElementById('sel-car').textContent=carac(s.animo);
    seg('s-fel',s.felicidad);seg('s-ene',s.energia);seg('s-abu',s.aburrimiento);seg('s-vit',s.energiaPers);
    document.getElementById('url').textContent='WWW.RAMONCITO.LOCAL / '+(s.ip||'CHARACTER');
    document.getElementById('sys').textContent='v'+s.fw+'  .  '+(s.edadDias<0?'0':s.edadDias)+' DIAS  .  '+(s.ssid||'-');
    document.getElementById('ico-snd').innerHTML=(s.sonido?'🔊':'🔇');
    var upd=document.getElementById('upd'),bang=document.getElementById('bang');
    if(s.hayUpdate){upd.style.display='block';
      upd.innerHTML='Nueva version v'+s.verNueva+' - <b onclick="act(\'ota_install\',\'Instalar la nueva version? El toy se reinicia.\')">INSTALAR</b>';
      bang.style.display='flex';}
    else{upd.style.display='none';bang.style.display='none';}
  }catch(err){}
}
async function act(doName,confirmMsg){
  if(confirmMsg&&!confirm(confirmMsg))return;
  var q='/api/action?do='+doName+(doName==='renacer'?'&confirm=1':'');
  try{var r=await fetch(q);var j=await r.json();toast(j.msg);}catch(e){toast('error de red');}
  setTimeout(refresh,900);
}
setInterval(refresh,2000);refresh();
</script>
</body>
</html>)rawhtml");
}

