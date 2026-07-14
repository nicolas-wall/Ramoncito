// =============================================================
//  espToy — main.cpp
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

// ----- Display -----------------------------------------------
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ----- Máquina de estados global ------------------------------
enum class AppState : uint8_t { IDLE, REACTING, SLEEPING, MENU, STANDBY };
static AppState appState = AppState::IDLE;

static uint32_t menuHasta = 0;
static const uint32_t MENU_TIMEOUT_MS = 10000;
static uint8_t  menuPagina = 1;  // paginación del menú: 1 o 2 (Etapa C)

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
    for (uint8_t addr = 1; addr < 127; addr++) {
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
static void entrarStandby() {
    randExprActiva = false;
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
                menuPagina = 1;  // reiniciar en página 1 (Etapa C)
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

        // En el menú: botón abre la siguiente página o cierra
        if (appState == AppState::MENU) {
            if (ev == InputEvent::BTN_A_PRESS) {
                ultimoBotonMs = ahora;
                if (menuPagina == 1) {
                    // Pasar a página 2
                    menuPagina = 2;
                    menuHasta = ahora + MENU_TIMEOUT_MS;
                    sound.play(Melody::BIP);
                } else if (menuPagina == 2) {
                    // Pasar a página 3
                    menuPagina = 3;
                    menuHasta = ahora + MENU_TIMEOUT_MS;
                    sound.play(Melody::BIP);
                } else {
                    // menuPagina == 3: cerrar menú
                    appState = AppState::IDLE;
                    idleExprActual = mood.dominantExpression();
                    face.setExpression(idleExprActual);
                    sound.play(Melody::BIP);
                    marcarActividad(ahora);
                    entroADormirMs  = ahora;
                }
            } else if (menuPagina == 3) {
                // Acciones táctiles solo en página 3
                bool enLockout = (ultimoBotonMs != 0) &&
                                 ((ahora - ultimoBotonMs) < TOUCH_LOCKOUT_BOTON_MS);
                if (enLockout) {
                    Serial.println("[app] touch ignorado (lockout boton, menu p3)");
                } else if (ev == InputEvent::TOUCH_START) {
                    // Cabeza: cerrar menú y abrir portal WiFi
                    appState = AppState::IDLE;
                    idleExprActual = mood.dominantExpression();
                    face.setExpression(idleExprActual);
                    marcarActividad(ahora);
                    entroADormirMs = ahora;
                    net.startPortal();
                    sound.play(Melody::BIP);
                    reaccionar(Expression::SORPRENDIDO, 3000, "portal: espToy-setup", ahora);
                } else if (ev == InputEvent::TICKLE_START && ota.hayActualizacion()) {
                    // Pie: instalar actualización (bloqueante)
                    sound.play(Melody::BIP);
                    ota.instalarAhora();
                    // Si vuelve acá, la instalación falló: cerrar el menú limpiamente
                    appState = AppState::IDLE;
                    idleExprActual = mood.dominantExpression();
                    face.setExpression(idleExprActual);
                    marcarActividad(ahora);
                    entroADormirMs = ahora;
                }
            }
            continue;
        }

        switch (ev) {
            case InputEvent::BTN_A_PRESS:
                ultimoBotonMs = ahora;
                marcarActividad(ahora);
                randExprActiva  = false;
                appState  = AppState::MENU;
                menuPagina = 1;  // reiniciar en página 1 (Etapa C)
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
                        reaccionar(Expression::ENOJADO, REACCION_BTN_MS, "Basta!", ahora);
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
                            reaccionar(Expression::ENOJADO, REACCION_BTN_MS, "Basta!", ahora);
                        } else {
                            mood.apply(MoodEffect::COSQUILLAS);
                            personality.event(PersEvent::COSQUILLAS_OK);
                            sound.play(Melody::FELIZ);
                            reaccionar(Expression::RISA, REACCION_TICKLE_MS, "jajaja", ahora);
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
    Serial.printf("[pers] alegre:%u grunon:%u energetico:%u perezoso:%u | edad:%d dias | factor:%.2f\n",
                  personality.alegre(), personality.grunon(),
                  personality.energetico(), personality.perezoso(),
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
            menuPagina = 1;  // reiniciar en página 1 (Etapa C)
            menuHasta = millis() + MENU_TIMEOUT_MS;
        }
        Serial.printf("[cmd] menu: %d\n", appState == AppState::MENU);
    } else if (cmd[0] == 'q') {
        int a, g, e, p;
        if (sscanf(cmd + 1, "%d %d %d %d", &a, &g, &e, &p) == 4) {
            // q A G E P → fija los 4 rasgos directamente
            personality.set((uint8_t)a, (uint8_t)g, (uint8_t)e, (uint8_t)p);
            Serial.printf("[cmd] personalidad fijada A:%d G:%d E:%d P:%d\n", a, g, e, p);
        } else {
            // q solo → imprime rasgos + edad + factor
            Serial.printf("[pers] alegre:%u grunon:%u energetico:%u perezoso:%u | edad:%d dias | factor:%.2f\n",
                          personality.alegre(), personality.grunon(),
                          personality.energetico(), personality.perezoso(),
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
    Serial.printf ("  espToy boot OK | FW: %s\n", FW_VERSION);
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

    Serial.println("[app] listo — comandos: h N | m F E A | s | p | i | n | q | q A G E P | u (OTA)");
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
    net.update(ahora);

    // Auto-OTA: solo fuera del menú para no pisar su render con la pantalla
    // de progreso. En IDLE o SLEEPING el chequeo bloqueante es aceptable.
    if (appState != AppState::MENU) {
        ota.update(ahora);
    }

    // Descansando = sueño nocturno o siesta por agotamiento (cara DORMIDO):
    // en vez de decaer, recupera energía de a poco.
    bool descansando = (appState == AppState::SLEEPING) ||
                       (appState == AppState::IDLE && idleExprActual == Expression::DORMIDO);
    mood.update(ahora, descansando);
    personality.update(ahora);
    sound.update(ahora);

    // Hora recién validada -> decaimiento offline (una sola vez)
    if (net.justGotValidTime()) {
        time_t nowEpoch = time(nullptr);
        mood.applyOfflineDecay(nowEpoch);
        mood.noteTimeValid(nowEpoch);
        personality.noteTimeValid(nowEpoch);
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

    // En standby: no hay más lógica ni render
    if (appState == AppState::STANDBY) {
        return;
    }

    // Auto-cierre del menú
    if (appState == AppState::MENU && (int32_t)(ahora - menuHasta) >= 0) {
        appState = AppState::IDLE;
        menuPagina = 1;  // resetear paginación (Etapa C)
        idleExprActual = mood.dominantExpression();
        face.setExpression(idleExprActual);
        marcarActividad(ahora);
        entroADormirMs  = ahora;  // después del menú, 30 min antes de standby
    }

    // Transiciones dependientes de la hora
    bool esNoche = net.isNight();
    bool fueraDeLaGracia = (int32_t)(ahora - standbyGraciaHasta) >= 0;
    if (appState == AppState::IDLE && esNoche && fueraDeLaGracia) entrarADormir(ahora);
    else if (appState == AppState::SLEEPING && !esNoche)           despertar(ahora);

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

    // Ronquido periódico durmiendo
    if (appState == AppState::SLEEPING && (int32_t)(ahora - proximoRonquido) >= 0) {
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
        md.fwVersion    = FW_VERSION;
        md.staConectada = net.staConnected();
        md.hayUpdate    = ota.hayActualizacion();
        md.versionNueva = ota.versionNueva();
        // Campos de personalidad (Etapa C)
        md.alegre      = personality.alegre();
        md.grunon      = personality.grunon();
        md.energetico  = personality.energetico();
        md.perezoso    = personality.perezoso();
        md.edadDias    = personality.edadDias();
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
        Serial.printf("espToy | fps:%lu heap:%lu | F:%u E:%u A:%u | hora:%d noche:%d | est:%d | tc:%lu/%lu tp:%lu/%lu\n",
                      (unsigned long)fpsActual, (unsigned long)ESP.getFreeHeap(),
                      mood.happiness(), mood.energy(), mood.boredom(),
                      net.hourNow(), esNoche, (int)appState,
                      (unsigned long)input.touchValue(),    (unsigned long)input.touchBaseline(),
                      (unsigned long)input.touchValuePie(), (unsigned long)input.touchBaselinePie());
    }
}
