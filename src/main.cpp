// =============================================================
//  Ramoncito — main.cpp
//  Orquestador: máquina de estados, eventos, mood/sound/net.
//
//  Interacciones (Etapa A — doc 06 §1):
//    Caricia (D2) → AMOR + buen humor. Durante malhumor: perdona (÷2 tiempo restante).
//    Cosquillas (D8) → FELIZ hasta TICKLE_SEGUIDAS_MAX; después ENOJADO.
//      Tras el enojo entra en malhumor (MALHUMOR_MS). Durante malhumor:
//        cosquillas → renuevan el enojo y extienden el malhumor.
//      Al expirar el malhumor: ticklesSeguidos vuelve a 0.
//    Botón (D0) → menú de estado (utilidad, nunca afecta humor).
//    De noche (SLEEPING):
//      1.ª caricia → cara FELIZ 2 s sin salir de SLEEPING.
//      2.ª caricia en ≤30 s, o cualquier cosquilla → enojo nocturno 2.5 s, vuelve a DORMIDO.
//    Inactividad: cada 30 min → cara SOSPECHOSO 4 s (§1.4).
//
//  Comandos seriales:
//    h N       fuerza la hora; h -1 libera
//    m F E A   fija humor (0-100)
//    s         alterna sonido on/off
//    p         fuerza el portal WiFi
//    i         imprime estado (incluye rasgos de personalidad)
//    q         imprime rasgos + edad + factor de plasticidad
//    q A G E P fija los 4 rasgos directamente (para pruebas)
//    u         fuerza chequeo de auto-OTA inmediatamente
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <time.h>
#include "config.h"
#include "face.h"
#include "input.h"
#include "sound.h"
#include "mood.h"
#include "personality.h"
#include "net.h"
#include "menu.h"
#include "ota.h"
#include "imu.h"

// ----- Display -----------------------------------------------
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ----- Máquina de estados global ------------------------------
enum class AppState : uint8_t { IDLE, REACTING, SLEEPING, MENU, STANDBY, NACIENDO };
static AppState appState = AppState::IDLE;

static uint32_t menuHasta = 0;
static const uint32_t MENU_TIMEOUT_MS = 10000;
static uint8_t  menuPagina = 1;  // paginación del menú: 1..4
static uint8_t  ajustesSel = 0;  // opción resaltada en página 4 (0=Sonido,1=Renacer,2=WiFi)

static uint32_t    reaccionHasta   = 0;
static const char* reaccionLabel   = "";
static bool        reaccionEsTouch = false;

static Expression  idleExprActual  = Expression::NEUTRAL;
static uint32_t    proximoRonquido = 0;

// ----- Cosquillas seguidas y malhumor (§1.2) ------------------
static uint8_t  ticklesSeguidos = 0;
static uint32_t ultimoTickle    = 0;
static uint32_t malhumorHasta   = 0;  // 0 = sin malhumor; > 0 = timestamp de expiración

// ----- Bloqueo táctil tras presionar botón ----------------------
static uint32_t ultimoBotonMs   = 0;  // timestamp del último BTN_A_PRESS

// ----- Long-press del botón en menú página 3 -------------------
static bool     btnPulsado       = false;  // botón está físicamente presionado
static uint32_t btnPulsadoDesde  = 0;      // millis() cuando se detectó el press
static bool     longPressEjecutado = false; // flag: ya se hizo el toggle, ignorar release

// ----- Renacer: confirmación de doble toque en página 2 -------
static bool     renacerConfirmando  = false; // esperando pie para confirmar
static uint32_t renacerTimeoutHasta = 0;     // millis límite para confirmar

// ----- Animación de nacimiento --------------------------------
static uint32_t nacimientoInicioMs = 0;  // millis() cuando empezó la anim
static uint8_t  nacimientoFase     = 0;  // fase actual (0, 1, 2)

// ----- Expresiones aleatorias idle ----------------------------
static uint32_t sigGuino       = 0;
static uint32_t sigSospechoso  = 0;
static uint32_t randExprHasta  = 0;
static bool     randExprActiva = false;

// ----- Interacciones nocturnas (§1.3) --------------------------
static uint32_t ultimaCariciaNoche = 0;  // millis de la última caricia nocturna
static uint32_t caraNocheHasta     = 0;  // 0 = inactiva; > 0 = volver a DORMIDO cuando venza

// ----- Inactividad y standby ----------------------------------
static uint32_t ultimaActividad    = 0;  // millis del último evento de interacción
static uint32_t sigQuePasa         = 0;  // próximo disparo de cara SOSPECHOSO por inactividad (§1.4)
static uint32_t entroADormirMs     = 0;  // millis cuando empezó a dormir
static uint32_t standbyGraciaHasta = 0;  // bloquea la transición a dormir tras salir del standby

// ----- Muestreo pasivo de personalidad (§3.2) -----------------
// Se dispara cada MOOD_TICK_MS/TIME_SCALE, igual que el tick de humor,
// pero solo durante IDLE (despierto o siesta). El sueño nocturno no cuenta.
static uint32_t sigMuestraPers = 0;

// ----- Estado varios ------------------------------------------
static uint32_t ultimoLog = 0, ultimoFrame = 0;
static uint32_t framesEnVentana = 0, fpsActual = 0;
static char     cmdBuf[32];
static uint8_t  cmdLen = 0;

// ------------------------------------------------------------
static void scheduleGuino(uint32_t ahora) {
    sigGuino = ahora + GUINO_RAND_MIN_MS +
               (uint32_t)(random((long)(GUINO_RAND_MAX_MS - GUINO_RAND_MIN_MS)));
}
static void scheduleSospechoso(uint32_t ahora) {
    sigSospechoso = ahora + SOSP_RAND_MIN_MS +
                    (uint32_t)(random((long)(SOSP_RAND_MAX_MS - SOSP_RAND_MIN_MS)));
}

// Registra cualquier interacción: actualiza ultimaActividad y
// reinicia el contador del "qué pasa" (§1.4).
static void marcarActividad(uint32_t ahora) {
    ultimaActividad = ahora;
    sigQuePasa      = ahora + INACTIVIDAD_QUEHACER_MS;
}

// ------------------------------------------------------------
static void scanI2C() {
    Serial.println("[i2c] escaneando bus...");
    // Solo rango válido 0x08..0x77; las direcciones <0x08 y >0x77 son
    // reservadas y generan falsos positivos (ej. el "fantasma" en 0x01).
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0)
            Serial.printf("[i2c] dispositivo en 0x%02X\n", addr);
    }
}

// ------------------------------------------------------------
static void reaccionar(Expression e, uint32_t duracionMs,
                       const char* label, uint32_t ahora, bool esTouch = false) {
    randExprActiva  = false;
    marcarActividad(ahora);
    face.setExpression(e);
    reaccionHasta   = ahora + duracionMs;
    reaccionLabel   = label;
    reaccionEsTouch = esTouch;
    appState        = AppState::REACTING;
}

// ------------------------------------------------------------
static void entrarADormir(uint32_t ahora) {
    randExprActiva = false;
    appState = AppState::SLEEPING;
    face.setExpression(Expression::DORMIDO);
    sound.play(Melody::DORMIR);
    proximoRonquido = ahora + 3000;
    entroADormirMs  = ahora;
    Serial.println("[app] a dormir...");
}

static void despertar(uint32_t ahora) {
    appState = AppState::IDLE;
    idleExprActual = mood.dominantExpression();
    face.setExpression(idleExprActual);
    sound.play(Melody::DESPERTAR);
    marcarActividad(ahora);
    scheduleGuino(ahora);
    scheduleSospechoso(ahora);
    randExprActiva = false;
    caraNocheHasta = 0;  // limpiar estado nocturno al despertar
    Serial.println("[app] buen dia!");
}

// ------------------------------------------------------------
static void dispararNacimiento(uint32_t ahora) {
    // Arranca la animación de encendido CRT (no bloqueante).
    // Fase 0 (~260 ms): línea horizontal crece desde el centro.
    // Fase 1 (~560 ms): apertura vertical hacia pantalla llena (flash).
    // Fase 2 (~480 ms): ruido de estática/sintonía.
    // Fase 3 (~820 ms): revelado de cara con barrido de scanline.
    // Al terminar: IDLE con expresión FELIZ. Total ~2.1 s.
    appState           = AppState::NACIENDO;
    nacimientoInicioMs = ahora;
    nacimientoFase     = 0;
    // No mostramos DORMIDO; la pantalla arranca en negro (fase 0 lo dibuja).
    // Preparamos la expresión FELIZ para la fase 3, pero no la enviamos aún.
    face.setExpression(Expression::FELIZ);
    // TV_ON cubre toda la animación: pop → calentamiento → estática → chillido CRT → chirp final.
    sound.play(Melody::TV_ON);
    randExprActiva     = false;
    Serial.println("[app] animacion nacimiento CRT iniciada");
}

// Nombre corto de la expresión para el panel web (WebData.expresion).
static const char* nombreExpresion(Expression e) {
    switch (e) {
        case Expression::FELIZ:      return "feliz";
        case Expression::TRISTE:     return "triste";
        case Expression::ENOJADO:    return "enojado";
        case Expression::SORPRENDIDO:return "sorprendido";
        case Expression::ABURRIDO:   return "aburrido";
        case Expression::DORMIDO:    return "dormido";
        case Expression::SOSPECHOSO: return "sospechoso";
        case Expression::AMOR:       return "enamorado";
        case Expression::RISA:       return "riendo";
        case Expression::MAREADO:    return "mareado";
        case Expression::ILUSIONADO: return "ilusionado";
        default:                     return "tranquilo";
    }
}

// ------------------------------------------------------------
static void entrarStandby() {
    randExprActiva     = false;
    btnPulsado         = false;   // cancelar long-press pendiente
    longPressEjecutado = false;
    renacerConfirmando = false;   // cancelar confirmación de renacer si estaba activa
    appState = AppState::STANDBY;
    u8g2.setPowerSave(1);
    Serial.println("[app] standby — pantalla apagada");
}

static void salirStandby(uint32_t ahora) {
    u8g2.setPowerSave(0);
    marcarActividad(ahora);
    entroADormirMs     = ahora;          // resetea el timer de sueño
    standbyGraciaHasta = ahora + 30000;  // 30 s sin poder volver a dormir
    scheduleGuino(ahora);
    scheduleSospechoso(ahora);
    randExprActiva = false;
    // Siempre vuelve a IDLE para que el usuario pueda interactuar
    appState = AppState::IDLE;
    idleExprActual = mood.dominantExpression();
    face.setExpression(idleExprActual);
    Serial.println("[app] standby — pantalla encendida (gracia 30 s)");
}

// ------------------------------------------------------------
static void despacharEventos(uint32_t ahora) {
    InputEvent ev;
    while ((ev = input.nextEvent()) != InputEvent::NONE) {

        // Standby: cualquier toque o botón despierta la pantalla
        if (appState == AppState::STANDBY) {
            if (ev == InputEvent::BTN_A_PRESS) {
                ultimoBotonMs = ahora;
            }
            salirStandby(ahora);
            continue;
        }

        // Dormido: el botón abre el menú (utilidad); caricia y cosquilla
        // muestran una reacción facial breve sin cambiar appState (§1.3).
        if (appState == AppState::SLEEPING) {
            if (ev == InputEvent::BTN_A_PRESS) {
                ultimoBotonMs = ahora;
                appState  = AppState::MENU;
                menuPagina = 1;  // reiniciar en página 1
                ajustesSel = 0;
                menuHasta = ahora + MENU_TIMEOUT_MS;
                sound.play(Melody::BIP);
            } else if (ev == InputEvent::TOUCH_START) {
                // Ignorar touch si estamos en lockout del botón
                if ((ahora - ultimoBotonMs) < TOUCH_LOCKOUT_BOTON_MS && ultimoBotonMs != 0) {
                    Serial.println("[app] touch ignorado (lockout boton)");
                } else {
                    bool dentroDeVentana = (ahora - ultimaCariciaNoche) <= CARICIA_NOCHE_VENTANA_MS
                                           && ultimaCariciaNoche != 0;
                    if (!dentroDeVentana) {
                        // 1.ª caricia: cara FELIZ sin despertar
                        ultimaCariciaNoche = ahora;
                        face.setExpression(Expression::FELIZ);
                        caraNocheHasta = ahora + REACCION_NOCHE_FELIZ_MS;
                    } else {
                        // 2.ª caricia dentro de la ventana: enojo nocturno.
                        // Renueva la ventana: seguir acariciando lo sigue enojando.
                        ultimaCariciaNoche = ahora;
                        personality.event(PersEvent::ENOJO_NOCTURNO);
                        face.setExpression(Expression::ENOJADO);
                        sound.play(Melody::ENOJADO);
                        caraNocheHasta = ahora + REACCION_NOCHE_ENOJO_MS;
                    }
                }
            } else if (ev == InputEvent::TICKLE_START) {
                // Ignorar tickle si estamos en lockout del botón
                if ((ahora - ultimoBotonMs) < TOUCH_LOCKOUT_BOTON_MS && ultimoBotonMs != 0) {
                    Serial.println("[app] touch ignorado (lockout boton)");
                } else {
                    // Cosquilla durmiendo: enojo nocturno directo
                    personality.event(PersEvent::ENOJO_NOCTURNO);
                    face.setExpression(Expression::ENOJADO);
                    sound.play(Melody::ENOJADO);
                    caraNocheHasta = ahora + REACCION_NOCHE_ENOJO_MS;
                }
            }
            continue;
        }

        // En el menú: botón navega páginas; los toques dependen de la página.
        if (appState == AppState::MENU) {

            // ── Overlay de confirmación de renacer: intercepta todo ──
            if (renacerConfirmando) {
                if (ev == InputEvent::BTN_A_PRESS) {
                    ultimoBotonMs = ahora;
                    renacerConfirmando = false;
                    sound.play(Melody::BIP);
                    menuHasta = ahora + MENU_TIMEOUT_MS;
                    Serial.println("[app] renacer cancelado (boton)");
                } else if (ev == InputEvent::TICKLE_START) {
                    bool enLockout = (ultimoBotonMs != 0) &&
                                     ((ahora - ultimoBotonMs) < TOUCH_LOCKOUT_BOTON_MS);
                    if (!enLockout) {
                        // Segundo toque (pie): CONFIRMAR renacer
                        renacerConfirmando = false;
                        time_t nowEpoch = net.timeValid() ? time(nullptr) : 0;
                        personality.renacer(nowEpoch);
                        mood.reset();
                        menuPagina = 1;
                        ajustesSel = 0;
                        dispararNacimiento(ahora);
                        Serial.println("[app] renacer CONFIRMADO — animacion nacimiento");
                    }
                }
                // TOUCH (cabeza) se ignora durante la confirmación
                continue;
            }

            if (ev == InputEvent::BTN_A_PRESS) {
                ultimoBotonMs = ahora;
                if (menuPagina < 4) {
                    // Avanzar de página
                    menuPagina++;
                    menuHasta = ahora + MENU_TIMEOUT_MS;
                    sound.play(Melody::BIP);
                } else {
                    // Última página: cerrar el menú
                    appState = AppState::IDLE;
                    idleExprActual = mood.dominantExpression();
                    face.setExpression(idleExprActual);
                    sound.play(Melody::BIP);
                    marcarActividad(ahora);
                    entroADormirMs = ahora;
                }
            } else {
                // Acciones táctiles según la página (con lockout tras botón)
                bool enLockout = (ultimoBotonMs != 0) &&
                                 ((ahora - ultimoBotonMs) < TOUCH_LOCKOUT_BOTON_MS);
                if (enLockout) {
                    Serial.println("[app] touch ignorado (lockout boton, menu)");
                } else if (menuPagina == 3) {
                    // Página 3: pie instala la actualización OTA (si hay)
                    if (ev == InputEvent::TICKLE_START && ota.hayActualizacion()) {
                        sound.play(Melody::BIP);
                        ota.instalarAhora();
                        // Si vuelve acá, la instalación falló: cerrar limpio
                        appState = AppState::IDLE;
                        idleExprActual = mood.dominantExpression();
                        face.setExpression(idleExprActual);
                        marcarActividad(ahora);
                        entroADormirMs = ahora;
                    }
                } else if (menuPagina == 4) {
                    // Página 4 (Ajustes): cabeza mueve el cursor, pie activa.
                    if (ev == InputEvent::TOUCH_START) {
                        ajustesSel = (ajustesSel + 1) % 3;
                        sound.play(Melody::BIP);
                        menuHasta = ahora + MENU_TIMEOUT_MS;
                    } else if (ev == InputEvent::TICKLE_START) {
                        menuHasta = ahora + MENU_TIMEOUT_MS;
                        if (ajustesSel == 0) {
                            // Sonido on/off
                            bool nuevo = !sound.enabled();
                            sound.setEnabled(nuevo);
                            Serial.printf("[app] ajustes: sonido %s\n", nuevo ? "ON" : "OFF");
                            if (nuevo) sound.play(Melody::BIP);
                        } else if (ajustesSel == 1) {
                            // Renacer: pedir confirmación (overlay)
                            renacerConfirmando  = true;
                            renacerTimeoutHasta = ahora + MENU_RENACER_CONFIRM_MS;
                            sound.play(Melody::BIP);
                            Serial.println("[app] renacer: aguardando confirmacion (pie=si, btn=no)");
                        } else {
                            // Cambiar WiFi: cerrar menú y abrir portal
                            appState = AppState::IDLE;
                            idleExprActual = mood.dominantExpression();
                            face.setExpression(idleExprActual);
                            marcarActividad(ahora);
                            entroADormirMs = ahora;
                            net.startPortal();
                            sound.play(Melody::BIP);
                            reaccionar(Expression::SORPRENDIDO, 3000, "portal: Ramoncito-setup", ahora);
                        }
                    }
                }
            }
            continue;
        }

        // NACIENDO: ignorar toda interacción durante la animación
        if (appState == AppState::NACIENDO) {
            continue;
        }

        switch (ev) {
            case InputEvent::BTN_A_PRESS:
                ultimoBotonMs = ahora;
                marcarActividad(ahora);
                randExprActiva  = false;
                appState  = AppState::MENU;
                menuPagina = 1;  // reiniciar en página 1
                ajustesSel = 0;
                menuHasta = ahora + MENU_TIMEOUT_MS;
                sound.play(Melody::BIP);
                break;

            case InputEvent::TOUCH_START: {
                // Ignorar touch si estamos en lockout del botón
                if ((ahora - ultimoBotonMs) < TOUCH_LOCKOUT_BOTON_MS && ultimoBotonMs != 0) {
                    Serial.println("[app] touch ignorado (lockout boton)");
                } else {
                    bool enMalhumor = (malhumorHasta != 0) && ((int32_t)(ahora - malhumorHasta) < 0);
                    mood.apply(MoodEffect::CARICIA);
                    personality.event(PersEvent::CARICIA);
                    sound.play(Melody::AMOR);
                    if (enMalhumor) {
                        // Perdona: corta el tiempo restante a la mitad (§1.2)
                        uint32_t restante = malhumorHasta - ahora;
                        malhumorHasta = ahora + restante / 2;
                    }
                    reaccionar(Expression::AMOR, REACCION_TOUCH_MS, "", ahora, true);
                }
                break;
            }

            case InputEvent::TICKLE_START: {
                // Ignorar tickle si estamos en lockout del botón
                if ((ahora - ultimoBotonMs) < TOUCH_LOCKOUT_BOTON_MS && ultimoBotonMs != 0) {
                    Serial.println("[app] touch ignorado (lockout boton)");
                } else {
                    bool enMalhumor = (malhumorHasta != 0) && ((int32_t)(ahora - malhumorHasta) < 0);
                    if (enMalhumor) {
                        // Durante malhumor: renueva el enojo, extiende el malhumor (§1.2)
                        // El tiempo de malhumor es dinámico según personalidad
                        malhumorHasta = ahora + personality.malhumorMs();
                        personality.event(PersEvent::ENOJO_COSQUILLAS);
                        sound.play(Melody::ENOJADO);
                        reaccionar(Expression::ENOJADO, REACCION_BTN_MS, "", ahora);
                        // No aplicar mood positivo; no incrementar ticklesSeguidos
                    } else {
                        marcarActividad(ahora);
                        if ((ahora - ultimoTickle) > TICKLE_VENTANA_MS) {
                            ticklesSeguidos = 0;
                        }
                        ticklesSeguidos++;
                        ultimoTickle = ahora;

                        // Límite dinámico: 2 cosquillas si gruñón alto, 3 en otro caso
                        if (ticklesSeguidos >= personality.tickleMax()) {
                            ticklesSeguidos = 0;
                            malhumorHasta = ahora + personality.malhumorMs();  // malhumor dinámico (§1.2)
                            mood.apply(MoodEffect::COSQUILLAS_SEGUIDAS);
                            personality.event(PersEvent::ENOJO_COSQUILLAS);
                            sound.play(Melody::ENOJADO);
                            reaccionar(Expression::ENOJADO, REACCION_BTN_MS, "", ahora);
                        } else {
                            mood.apply(MoodEffect::COSQUILLAS);
                            personality.event(PersEvent::COSQUILLAS_OK);
                            sound.play(Melody::RISA);  // carcajada ja-ja-ja
                            reaccionar(Expression::RISA, REACCION_TICKLE_MS, "", ahora);
                        }
                    }
                }
                break;
            }

            default:
                break;
        }
    }
}

// ------------------------------------------------------------
static void imprimirEstado() {
    Serial.printf("[info] FW %s | estado:%d | F:%u E:%u A:%u | hora:%d noche:%d horaValida:%d portal:%d | sonido:%d | touch:%lu/%lu pie:%lu/%lu\n",
                  FW_VERSION, (int)appState,
                  mood.happiness(), mood.energy(), mood.boredom(),
                  net.hourNow(), net.isNight(), net.timeValid(), net.portalActive(),
                  sound.enabled(),
                  (unsigned long)input.touchValue(),    (unsigned long)input.touchBaseline(),
                  (unsigned long)input.touchValuePie(), (unsigned long)input.touchBaselinePie());
    Serial.printf("[pers] animo:%u energia:%u | edad:%d dias | factor:%.2f\n",
                  personality.animo(), personality.energia(),
                  personality.edadDias(), personality.plasticidadFactor());
}

static void procesarComando(const char* cmd) {
    if (cmd[0] == 'h') {
        int h = atoi(cmd + 1);
        net.forceHour(h);
        Serial.printf("[cmd] hora forzada: %d (isNight=%d)\n", h, net.isNight());
    } else if (cmd[0] == 'm') {
        int f, e, a;
        if (sscanf(cmd + 1, "%d %d %d", &f, &e, &a) == 3) {
            mood.set((uint8_t)f, (uint8_t)e, (uint8_t)a);
            Serial.printf("[cmd] mood fijado F:%d E:%d A:%d\n", f, e, a);
        }
    } else if (cmd[0] == 's') {
        sound.setEnabled(!sound.enabled());
        Serial.printf("[cmd] sonido: %d\n", sound.enabled());
    } else if (cmd[0] == 'p') {
        net.startPortal();
        Serial.println("[cmd] portal forzado");
    } else if (cmd[0] == 'i') {
        imprimirEstado();
    } else if (cmd[0] == 'n') {
        // Alternar menú (equivale a apretar un botón)
        if (appState == AppState::MENU) {
            appState = AppState::IDLE;
            face.setExpression(mood.dominantExpression());
        } else {
            appState  = AppState::MENU;
            menuPagina = 1;  // reiniciar en página 1
            ajustesSel = 0;
            menuHasta = millis() + MENU_TIMEOUT_MS;
        }
        Serial.printf("[cmd] menu: %d\n", appState == AppState::MENU);
    } else if (cmd[0] == 'q') {
        int an, en;
        if (sscanf(cmd + 1, "%d %d", &an, &en) == 2) {
            // q A E → fija los dos ejes (animo, energia)
            personality.set((uint8_t)an, (uint8_t)en);
            Serial.printf("[cmd] personalidad fijada animo:%d energia:%d\n", an, en);
        } else {
            // q solo → imprime ejes + edad + factor
            Serial.printf("[pers] animo:%u energia:%u | edad:%d dias | factor:%.2f\n",
                          personality.animo(), personality.energia(),
                          personality.edadDias(), personality.plasticidadFactor());
        }
    } else if (cmd[0] == 'u') {
        // Forzar chequeo de auto-OTA inmediatamente
        ota.forzarChequeo();
        Serial.println("[cmd] chequeo OTA forzado");
    }
}

static void leerSerial() {
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (c == '\n' || c == '\r') {
            if (cmdLen > 0) { cmdBuf[cmdLen] = '\0'; procesarComando(cmdBuf); cmdLen = 0; }
        } else if (cmdLen < sizeof(cmdBuf) - 1) {
            cmdBuf[cmdLen++] = c;
        }
    }
}

// ------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1500);

    Serial.println("=========================================");
    Serial.printf ("  Ramoncito boot OK | FW: %s\n", FW_VERSION);
    Serial.println("=========================================");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);

    Wire.begin();
    Wire.setClock(I2C_CLOCK_HZ);
    scanI2C();

    if (!u8g2.begin()) Serial.println("[oled] ERROR: begin() fallo");
    else               Serial.println("[oled] SSD1309 OK");
    u8g2.setBusClock(I2C_CLOCK_HZ);

    sound.begin();
    mood.begin();
    personality.begin();
    input.begin();
    face.begin();
    imu.begin();   // requiere Wire ya iniciado; falla silenciosamente si no hay MPU
    net.begin();
    ota.begin(&u8g2);

    idleExprActual = mood.dominantExpression();
    face.setExpression(idleExprActual);
    sound.play(Melody::BOOT);

    uint32_t ahora = millis();
    marcarActividad(ahora);
    scheduleGuino(ahora);
    scheduleSospechoso(ahora);
    sigMuestraPers = ahora + MOOD_TICK_MS / TIME_SCALE;

    Serial.println("[app] listo — comandos: h N | m F E A | s | p | i | n | q | q A E | u (OTA)");
}

// ------------------------------------------------------------
void loop() {
    uint32_t ahora = millis();

    input.poll(ahora);
    leerSerial();

    // (El LED de latido de la Etapa 0 quedó desactivado a pedido del
    //  usuario; el pin queda en HIGH = apagado desde setup().)

    if (ahora - ultimoFrame < FRAME_MS) return;
    ultimoFrame = ahora;
    framesEnVentana++;

    // Módulos de fondo
    imu.update(ahora);
    net.update(ahora);

    // Auto-OTA: solo fuera del menú para no pisar su render con la pantalla
    // de progreso. En IDLE o SLEEPING el chequeo bloqueante es aceptable.
    if (appState != AppState::MENU) {
        ota.update(ahora);
    }

    // Descansando = sueño nocturno, standby (pantalla apagada por inactividad)
    // o siesta por agotamiento (cara DORMIDO): en vez de decaer, recupera
    // energía de a poco. Sin STANDBY la energía caía a 0 tras un día sin uso.
    bool descansando = (appState == AppState::SLEEPING) ||
                       (appState == AppState::STANDBY) ||
                       (appState == AppState::IDLE && idleExprActual == Expression::DORMIDO);
    mood.update(ahora, descansando);
    personality.update(ahora);
    sound.update(ahora);

    // ── Panel web en la LAN ─────────────────────────────────────
    // Refrescar el snapshot que lee el dashboard (barato, cada frame).
    {
        WebData wd;
        wd.felicidad    = mood.happiness();
        wd.energia      = mood.energy();
        wd.aburrimiento = mood.boredom();
        wd.animo        = personality.animo();
        wd.energia_pers = personality.energia();
        wd.edadDias     = personality.edadDias();
        wd.sonido       = sound.enabled();
        wd.hayUpdate    = ota.hayActualizacion();
        snprintf(wd.fwVersion,    sizeof(wd.fwVersion),    "%s", FW_VERSION);
        snprintf(wd.versionNueva, sizeof(wd.versionNueva), "%s", ota.versionNueva());
        snprintf(wd.expresion,    sizeof(wd.expresion),    "%s", nombreExpresion(idleExprActual));
        net.setWebData(wd);
    }

    // Acciones disparadas desde el panel web (se ejecutan acá, fuera del
    // handler HTTP, para no bloquear ni reiniciar en medio de la respuesta).
    switch (net.takeWebAction()) {
        case WebAction::TOGGLE_SONIDO:
            sound.setEnabled(!sound.enabled());
            Serial.printf("[web] sonido: %s\n", sound.enabled() ? "ON" : "OFF");
            if (sound.enabled()) sound.play(Melody::BIP);
            break;
        case WebAction::OTA_CHECK:
            Serial.println("[web] chequeo OTA forzado");
            ota.forzarChequeo();
            break;
        case WebAction::OTA_INSTALL:
            if (ota.hayActualizacion()) {
                Serial.println("[web] instalando OTA desde el panel");
                ota.instalarAhora();   // bloqueante + reinicio si OK
            }
            break;
        case WebAction::ABRIR_PORTAL:
            Serial.println("[web] abriendo portal WiFi desde el panel");
            net.startPortal();
            break;
        case WebAction::RENACER: {
            Serial.println("[web] RENACER confirmado desde el panel");
            time_t nowEpoch = net.timeValid() ? time(nullptr) : 0;
            personality.renacer(nowEpoch);
            mood.reset();
            menuPagina = 1;
            ajustesSel = 0;
            renacerConfirmando = false;
            dispararNacimiento(ahora);
            break;
        }
        case WebAction::NINGUNA:
        default:
            break;
    }

    // Hora recién validada -> decaimiento offline (una sola vez)
    if (net.justGotValidTime()) {
        time_t nowEpoch = time(nullptr);
        mood.applyOfflineDecay(nowEpoch);
        mood.noteTimeValid(nowEpoch);
        // Si el juguete aún no tenía fecha de nacimiento, esta es la primera
        // vez que se conoce la hora → registrar nacimiento y animar.
        bool eraSinNacimiento = (personality.edadDias() < 0);
        personality.noteTimeValid(nowEpoch);
        if (eraSinNacimiento && personality.edadDias() >= 0 &&
            appState == AppState::IDLE) {
            // Primera hora válida: disparar animación de nacimiento
            dispararNacimiento(ahora);
        }
    }

    // Muestreo pasivo de personalidad: cada tick de humor, solo en IDLE
    // (despierto o siesta diurna). El sueño nocturno (SLEEPING) no cuenta.
    if (appState != AppState::SLEEPING &&
        appState != AppState::MENU &&
        appState != AppState::STANDBY &&
        (int32_t)(ahora - sigMuestraPers) >= 0) {
        sigMuestraPers = ahora + MOOD_TICK_MS / TIME_SCALE;
        personality.sampleTick(idleExprActual, descansando);
    }

    despacharEventos(ahora);

    // (El toggle de sonido por long-press se retiró: ahora vive como
    //  opción de la página 4 · Ajustes.)

    // ── Eventos IMU (acelerómetro MPU6050) ──────────────────────
    // Solo se procesan fuera del MENU (igual que los toques táctiles).
    if (appState != AppState::MENU && imu.habilitado()) {

        // ── LEVANTADO: reacción según personalidad ───────────────
        if (imu.huboLevantado()) {
            marcarActividad(ahora);
            if (appState == AppState::STANDBY) {
                salirStandby(ahora);
            } else if (appState == AppState::SLEEPING) {
                // Levantado de noche: reacción breve sin despertar del todo
                face.setExpression(Expression::SORPRENDIDO);
                sound.play(Melody::BIP);
                caraNocheHasta = ahora + 1500;
                Serial.println("[imu] levantado (durmiendo) → SORPRENDIDO breve");
            } else {
                // Despierto: cara única de "me alzaron" (ILUSIONADO), sin texto.
                mood.apply(MoodEffect::LEVANTADO);
                personality.event(PersEvent::LEVANTADO);
                sound.play(Melody::FELIZ);
                reaccionar(Expression::ILUSIONADO, REACCION_TOUCH_MS, "", ahora);
                Serial.println("[imu] levantado → ILUSIONADO");
            }
        }

        // ── SACUDIDA: leve / excesiva según personalidad ─────────
        if (imu.huboSacudida()) {
            marcarActividad(ahora);
            if (appState == AppState::STANDBY) {
                salirStandby(ahora);
            } else if (appState == AppState::SLEEPING) {
                // Sacudida durmiendo: enojo nocturno directo
                personality.event(PersEvent::ENOJO_NOCTURNO);
                face.setExpression(Expression::ENOJADO);
                sound.play(Melody::ENOJADO);
                caraNocheHasta = ahora + REACCION_NOCHE_ENOJO_MS;
                Serial.println("[imu] sacudida (durmiendo) → ENOJADO nocturno");
            } else {
                // Despierto: umbral de "excesiva" ajustado por personalidad.
                // Un gruñón se marea con menos sacudidas; un energético aguanta más.
                bool esGrunon    = Personality::esAlta(personality.grunon());
                bool esEnergetico = Personality::esAlta(personality.energetico());

                // Calcular umbral efectivo (mínimo 2 para que siempre haya zona "leve")
                int8_t umbralExcesiva = (int8_t)IMU_SACUDIDA_MAX;
                if (esGrunon)     umbralExcesiva -= 1;
                if (esEnergetico) umbralExcesiva += 1;
                if (umbralExcesiva < 2) umbralExcesiva = 2;

                if (imu.sacudidasEnVentana() >= (uint8_t)umbralExcesiva) {
                    // Sacudida excesiva → MAREADO + malhumor + sesgo a gruñón
                    malhumorHasta = ahora + personality.malhumorMs();
                    mood.apply(MoodEffect::SACUDIDA_EXCESIVA);
                    personality.event(PersEvent::ENOJO_COSQUILLAS);
                    sound.play(Melody::ENOJADO);
                    reaccionar(Expression::MAREADO, REACCION_BTN_MS, "", ahora);
                    Serial.printf("[imu] sacudida excesiva (%u, umbral=%d) → MAREADO + malhumor\n",
                                  imu.sacudidasEnVentana(), umbralExcesiva);
                } else {
                    // Sacudida leve → sorpresa (sin texto); estimulante, baja aburrimiento
                    mood.apply(MoodEffect::SACUDIDA_LEVE);
                    sound.play(Melody::BIP);
                    reaccionar(Expression::SORPRENDIDO, REACCION_TOUCH_MS, "", ahora);
                    Serial.printf("[imu] sacudida leve (%u) → SORPRENDIDO%s\n",
                                  imu.sacudidasEnVentana(), esGrunon ? " (grunon irritado)" : "");
                }
            }
        }
    }

    // Expiración del malhumor: resetea el contador de cosquillas (§1.2)
    if (malhumorHasta != 0 && (int32_t)(ahora - malhumorHasta) >= 0) {
        malhumorHasta   = 0;
        ticklesSeguidos = 0;
    }

    // Cara nocturna transitoria: volver a DORMIDO cuando venza (§1.3)
    if (appState == AppState::SLEEPING && caraNocheHasta != 0
        && (int32_t)(ahora - caraNocheHasta) >= 0) {
        face.setExpression(Expression::DORMIDO);
        caraNocheHasta = 0;
    }

    // Standby en IDLE por inactividad prolongada
    if (appState == AppState::IDLE &&
        (ahora - ultimaActividad) >= INACTIVIDAD_STANDBY_MS) {
        entrarStandby();
    }
    // Standby en SLEEPING después de DORMIDO_STANDBY_MS durmiendo
    if (appState == AppState::SLEEPING &&
        (ahora - entroADormirMs) >= DORMIDO_STANDBY_MS) {
        entrarStandby();
    }
    // NACIENDO no puede entrar en standby: ya retorna antes en el bloque de arriba

    // En standby: no hay más lógica ni render
    if (appState == AppState::STANDBY) {
        return;
    }

    // Timeout de confirmación del renacer (sin pie en 6 s → cancelar)
    if (renacerConfirmando && (int32_t)(ahora - renacerTimeoutHasta) >= 0) {
        renacerConfirmando = false;
        Serial.println("[app] renacer cancelado (timeout)");
    }

    // Auto-cierre del menú
    if (appState == AppState::MENU && (int32_t)(ahora - menuHasta) >= 0) {
        appState = AppState::IDLE;
        menuPagina = 1;  // resetear paginación (Etapa C)
        renacerConfirmando = false;  // cancelar confirmación pendiente
        idleExprActual = mood.dominantExpression();
        face.setExpression(idleExprActual);
        marcarActividad(ahora);
        entroADormirMs  = ahora;  // después del menú, 30 min antes de standby
        // Limpiar estado de long-press si el menú expiró con botón en p3
        btnPulsado         = false;
        longPressEjecutado = false;
    }

    // Transiciones dependientes de la hora
    bool esNoche = net.isNight();
    bool fueraDeLaGracia = (int32_t)(ahora - standbyGraciaHasta) >= 0;
    if (appState == AppState::IDLE && esNoche && fueraDeLaGracia) entrarADormir(ahora);
    else if (appState == AppState::SLEEPING && !esNoche)           despertar(ahora);

    // ── Animación de nacimiento CRT (AppState::NACIENDO) ─────────
    // Secuencia temporizada no bloqueante de 4 fases (ver config.h):
    //   Fase 0 [0, F0):              línea horizontal crece (punto→ancho total)
    //   Fase 1 [F0, F0+F1):          apertura vertical (línea→pantalla llena/flash)
    //   Fase 2 [F0+F1, F0+F1+F2):   estática CRT (ruido aleatorio)
    //   Fase 3 [F0+F1+F2, total):    revelado de cara con barrido descendente
    //   Al terminar → IDLE con FELIZ
    if (appState == AppState::NACIENDO) {
        uint32_t t = ahora - nacimientoInicioMs;

        // ── Transiciones de fase ──────────────────────────────────
        uint32_t t1 = ANIM_NACIMIENTO_F0_MS;
        uint32_t t2 = t1 + ANIM_NACIMIENTO_F1_MS;
        uint32_t t3 = t2 + ANIM_NACIMIENTO_F2_MS;

        if (nacimientoFase == 0 && t >= t1) {
            nacimientoFase = 1;
        } else if (nacimientoFase == 1 && t >= t2) {
            nacimientoFase = 2;
        } else if (nacimientoFase == 2 && t >= t3) {
            nacimientoFase = 3;
            // Sin bip extra: el chirp final de TV_ON ya cubre esta transición.
        } else if (nacimientoFase == 3 && t >= ANIM_NACIMIENTO_TOTAL_MS) {
            // Fin de animación → IDLE feliz
            appState       = AppState::IDLE;
            idleExprActual = Expression::FELIZ;
            face.setExpression(idleExprActual);
            marcarActividad(ahora);
            entroADormirMs = ahora;
            scheduleGuino(ahora);
            scheduleSospechoso(ahora);
            Serial.println("[app] animacion nacimiento CRT completa → IDLE FELIZ");
            // Caer en el render normal del frame actual (con cara FELIZ)
        }

        if (appState == AppState::NACIENDO) {
            // ── Render custom CRT — NO delegar en face.render() durante fases 0‑2 ──
            u8g2.clearBuffer();  // pantalla negra base

            if (nacimientoFase == 0) {
                // ── Fase 0: línea horizontal crece desde el centro ────────
                // Progreso 0.0→1.0 en la duración de la fase
                float p = (float)t / (float)ANIM_NACIMIENTO_F0_MS;
                if (p > 1.0f) p = 1.0f;
                // Ancho de la línea: de 2 px hasta 128 px
                int16_t lineaW = (int16_t)(2.0f + p * (128.0f - 2.0f));
                int16_t lineaX = (128 - lineaW) / 2;  // centrada horizontalmente
                int16_t lineaY = 32 - (ANIM_NACIMIENTO_LINEA_GROSOR / 2);  // centro vertical
                u8g2.setDrawColor(1);
                u8g2.drawBox(lineaX, lineaY, lineaW, ANIM_NACIMIENTO_LINEA_GROSOR);

            } else if (nacimientoFase == 1) {
                // ── Fase 1: apertura vertical de línea a pantalla llena ───
                // Progreso 0.0→1.0 en la duración de la fase 1
                float p = (float)(t - t1) / (float)ANIM_NACIMIENTO_F1_MS;
                if (p > 1.0f) p = 1.0f;
                // Altura crece de LINEA_GROSOR px hasta 64 px
                int16_t altBase = ANIM_NACIMIENTO_LINEA_GROSOR;
                int16_t altMax  = 64;
                int16_t alt = (int16_t)(altBase + p * (altMax - altBase));
                int16_t rectY = 32 - alt / 2;
                if (rectY < 0) rectY = 0;
                u8g2.setDrawColor(1);
                u8g2.drawBox(0, rectY, 128, alt);

            } else if (nacimientoFase == 2) {
                // ── Fase 2: estática/ruido CRT ───────────────────────────
                // Fondo negro (ya está limpio); dibujar píxeles aleatorios
                u8g2.setDrawColor(1);
                for (uint16_t i = 0; i < ANIM_NACIMIENTO_RUIDO_PX; i++) {
                    int16_t rx = (int16_t)(random(128));
                    int16_t ry = (int16_t)(random(64));
                    u8g2.drawPixel(rx, ry);
                }
                // Algunas scanlines horizontales tenues (filas completas esparcidas)
                for (uint8_t fila = 4; fila < 64; fila += 8) {
                    if (random(3) == 0) {  // 1 de cada 3 → aspecto irregular
                        u8g2.drawHLine(0, fila, 128);
                    }
                }

            } else {
                // ── Fase 3: revelado de cara con barrido descendente ──────
                // face ya tiene setExpression(FELIZ) desde dispararNacimiento.
                // Actualizamos la animación interna de la cara.
                face.update(ahora);
                // Renderizar la cara completa en el buffer
                face.render(u8g2);
                // Tapar la parte inferior que aún no se reveló con un rectángulo negro.
                // Progreso 0.0 (todo tapado) → 1.0 (cara completa).
                float p = (float)(t - t3) / (float)ANIM_NACIMIENTO_F3_MS;
                if (p > 1.0f) p = 1.0f;
                // La "cortina" negra baja: su borde superior empieza en y=0 y baja a y=64.
                int16_t reveladoY = (int16_t)(p * 64.0f);  // línea de revelado (0→64)
                if (reveladoY < 64) {
                    // Tapar las filas que aún no se revelan (debajo de la línea de barrido)
                    u8g2.setDrawColor(0);
                    u8g2.drawBox(0, reveladoY, 128, 64 - reveladoY);
                }
                u8g2.setDrawColor(1);  // restaurar color de dibujo
            }

            u8g2.sendBuffer();
            return;
        }
    }

    // Fin de reacción
    if (appState == AppState::REACTING) {
        bool vencida = (int32_t)(ahora - reaccionHasta) >= 0;
        if (reaccionEsTouch && input.touching())
            reaccionHasta = ahora + 500;
        else if (vencida) {
            reaccionLabel = "";
            if (esNoche) {
                entrarADormir(ahora);
            } else {
                appState = AppState::IDLE;
                idleExprActual = mood.dominantExpression();
                face.setExpression(idleExprActual);
                scheduleGuino(ahora);
                scheduleSospechoso(ahora);
            }
        }
    }

    // Expresiones aleatorias durante IDLE (no durante siesta — cara DORMIDO)
    if (appState == AppState::IDLE && idleExprActual != Expression::DORMIDO) {
        if (randExprActiva) {
            // Terminar expresión aleatoria y restaurar idle
            if ((int32_t)(ahora - randExprHasta) >= 0) {
                face.setExpression(idleExprActual);
                randExprActiva = false;
            }
        } else {
            // "Qué pasa" determinista: cada 30 min de inactividad continua (§1.4)
            bool quePasaDisparar = (sigQuePasa != 0) && ((int32_t)(ahora - sigQuePasa) >= 0);

            if ((int32_t)(ahora - sigGuino) >= 0) {
                randExprActiva = true;
                randExprHasta = ahora + RAND_EXPR_DUR_MS;
                face.setExpression(Expression::GUINO);
                scheduleGuino(ahora + RAND_EXPR_DUR_MS);
            } else if (quePasaDisparar || (int32_t)(ahora - sigSospechoso) >= 0) {
                randExprActiva = true;
                uint32_t dur = quePasaDisparar ? QUEHACER_EXPR_DUR_MS : RAND_EXPR_DUR_MS;
                randExprHasta = ahora + dur;
                face.setExpression(Expression::SOSPECHOSO);
                if (quePasaDisparar) {
                    // Avanzar al siguiente múltiplo de 30 min de inactividad
                    sigQuePasa += INACTIVIDAD_QUEHACER_MS;
                } else {
                    scheduleSospechoso(ahora + dur);
                }
            } else {
                // Actualizar expresión idle si el humor cambió (con override de malhumor §1.2)
                bool enMalhumor = (malhumorHasta != 0) && ((int32_t)(ahora - malhumorHasta) < 0);
                Expression dominante = enMalhumor ? Expression::ENOJADO : mood.dominantExpression();
                if (dominante != idleExprActual) {
                    idleExprActual = dominante;
                    face.setExpression(dominante);
                }
            }
        }
    }

    // Ronquido periódico durmiendo — solo durante los primeros
    // RONQUIDO_VENTANA_MS tras quedarse dormido (no se repite para siempre).
    if (appState == AppState::SLEEPING
        && (ahora - entroADormirMs) < RONQUIDO_VENTANA_MS
        && (int32_t)(ahora - proximoRonquido) >= 0) {
        sound.play(Melody::RONQUIDO);
        proximoRonquido = ahora + 2500 + (esp_random() % 1000);
    }

    // Animar la cara
    face.update(ahora);

    // Render
    u8g2.clearBuffer();
    if (appState == AppState::MENU) {
        MenuData md;
        md.felicidad       = mood.happiness();
        md.energia         = mood.energy();
        md.aburrimiento    = mood.boredom();
        md.horaValida      = net.timeValid();
        md.hora = md.minuto = 0;
        md.dia = md.mes = md.diaSemana = 0;
        if (md.horaValida) {
            struct tm ti;
            if (getLocalTime(&ti, 10)) {
                md.hora      = ti.tm_hour;
                md.minuto    = ti.tm_min;
                md.dia       = ti.tm_mday;
                md.mes       = ti.tm_mon + 1;
                md.diaSemana = ti.tm_wday;
            }
        }
        md.wifiConfigurada = net.hasCredentials();
        md.ssid            = net.ssidGuardado();
        md.portalActivo    = net.portalActive();
        // Campos de firmware / OTA (página 3)
        md.fwVersion         = FW_VERSION;
        md.staConectada      = net.staConnected();
        md.hayUpdate         = ota.hayActualizacion();
        md.versionNueva      = ota.versionNueva();
        md.sonidoHabilitado  = sound.enabled();
        // Panel web en la LAN: dirección para conectarse desde el teléfono.
        // Buffer local: vive hasta que menuRender() retorna (mismo scope).
        char lanIpBuf[20];
        snprintf(lanIpBuf, sizeof(lanIpBuf), "%s", net.lanIP().c_str());
        md.lanServerActivo   = net.lanServerActivo();
        md.lanIP             = lanIpBuf;
        // Personalidad — 2 ejes (alegre=ánimo, energetico=energía)
        md.alegre      = personality.animo();
        md.energetico  = personality.energia();
        md.edadDias    = personality.edadDias();
        // Sub-estado de confirmación de renacer (overlay)
        md.renacerConfirmando = renacerConfirmando;
        // Página 4 · Ajustes
        md.ajustesSel  = ajustesSel;
        menuRender(u8g2, md, menuPagina);
    } else {
        face.render(u8g2);
        if (appState == AppState::REACTING && reaccionLabel[0] != '\0') {
            u8g2.setFont(u8g2_font_5x7_tf);
            u8g2.drawStr(2, 8, reaccionLabel);  // arriba: baseline y=8, pegado a la izquierda
        }
    }
    u8g2.sendBuffer();

    // Log 1/s
    if (ahora - ultimoLog >= INTERVALO_LOG_MS) {
        ultimoLog = ahora;
        fpsActual = framesEnVentana * 1000 / INTERVALO_LOG_MS;
        framesEnVentana = 0;
        Serial.printf("Ramoncito | fps:%lu heap:%lu | F:%u E:%u A:%u | hora:%d noche:%d | est:%d | tc:%lu/%lu tp:%lu/%lu\n",
                      (unsigned long)fpsActual, (unsigned long)ESP.getFreeHeap(),
                      mood.happiness(), mood.energy(), mood.boredom(),
                      net.hourNow(), esNoche, (int)appState,
                      (unsigned long)input.touchValue(),    (unsigned long)input.touchBaseline(),
                      (unsigned long)input.touchValuePie(), (unsigned long)input.touchBaselinePie());
    }
}
