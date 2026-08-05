// =============================================================
//  Ramoncito — main.cpp
//  Orquestador del mini arcade: máquina de estados, eventos, render.
//
//  Qué es este aparato: un arcade de escritorio. Se juega con la palanca
//  y tres botones; cuando nadie juega, muestra una carita que reacciona
//  cada tanto y responde a que lo alcen o lo sacudan. Nada más.
//
//  Lo que fue y ya no es: arrancó como mascota tipo Tamagotchi, con
//  humor, personalidad que evolucionaba, mensajería por Telegram y un
//  dashboard web. Todo eso se retiró en la 0.11.0 — ver el README.
//
//  Controles:
//    Botón A (D0)  → abre el menú de sistema / avanza de página.
//    Botón B (D8)  → mueve el cursor.
//    Botón C (D2)  → activa; desde la cara, ENTRA AL ARCADE.
//    Palanca       → arriba/abajo mueven el cursor; su pulsador activa.
//    IMU           → alzarlo y sacudirlo dan reacciones faciales.
//
//  Comandos seriales:
//    h N       fuerza la hora; h -1 libera
//    s         alterna sonido on/off
//    p         fuerza el portal WiFi
//    i         imprime estado
//    u         fuerza chequeo de auto-OTA inmediatamente
//    n         alterna el menú
//    e         pasa a la siguiente expresión (para revisar las caras)
//    o         fuerza el standby (para probar cómo despierta)
//    g         vigilancia del acelerómetro on/off
//    k         escáner de pines (foto de todos los pines libres)
//    v         vigilancia de pines on/off (imprime cada flanco)
//    1 / 2 / 3 simulan los botones A / B / C
//    z         pulsador de la palanca
//    w x a d   palanca arriba/abajo/izquierda/derecha (stub sin hardware).
//              Es w-x-a-d y no WASD porque 's' ya es el toggle de sonido.
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <time.h>
#include "config.h"
#include "face.h"
#include "input.h"
#include "sound.h"
#include "net.h"
#include "menu.h"
#include "ota.h"
#include "imu.h"
#include "arcade.h"

// ----- Display -----------------------------------------------
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R2, U8X8_PIN_NONE);  // R2 = 180° (montaje invertido)

// ----- Máquina de estados global ------------------------------
enum class AppState : uint8_t { IDLE, REACTING, MENU, STANDBY, ENCENDIENDO, ARCADE };
static AppState appState = AppState::IDLE;

// ----- Menú de sistema ----------------------------------------
static uint32_t menuHasta = 0;
static const uint32_t MENU_TIMEOUT_MS = 10000;
static uint8_t  menuPagina = 1;  // 1 = info, 2 = ajustes
static uint8_t  ajustesSel = 0;  // 0 = Sonido, 1 = Cambiar WiFi

// ----- Reacción facial pasajera -------------------------------
static uint32_t    reaccionHasta = 0;
static const char* reaccionLabel = "";

// Cara de reposo del gabinete.
//
// Antes la elegía el humor: felicidad/energía/aburrimiento decidían entre
// FELIZ, TRISTE, ENOJADO o ABURRIDO. Al pasar a arcade se fueron los
// sensores táctiles que alimentaban esas variables, y quedaron las salidas
// sin entradas: el único resultado posible era una deriva lenta hacia la
// cara triste, sin manera de revertirla.
//
// Ahora el reposo es NEUTRAL y la vida de la carita sale de los gestos
// ocasionales del idle y de las reacciones del IMU. Este es el único lugar
// que hay que tocar si alguna vez se quiere otra cosa.
static inline Expression caraDeReposo() { return Expression::NEUTRAL; }

static Expression idleExprActual = Expression::NEUTRAL;

// ----- Animación de encendido CRT -----------------------------
// Era la animación de "nacimiento" de la mascota, disparada al renacer o
// al conocer la hora por primera vez. Sin mascota que nazca, quedó como lo
// que siempre pareció: el encendido de un televisor viejo, al bootear.
static uint32_t encendidoInicioMs = 0;
static uint8_t  encendidoFase     = 0;

// ----- Expresiones aleatorias durante el idle -----------------
static uint32_t sigGuino       = 0;
static uint32_t sigSospechoso  = 0;
static uint32_t randExprHasta  = 0;
static bool     randExprActiva = false;

// ----- Inactividad y standby ----------------------------------
static uint32_t ultimaActividad = 0;  // millis del último evento de interacción
static uint32_t sigQuePasa      = 0;  // próximo disparo de cara SOSPECHOSO por inactividad

// ----- Diagnóstico de pines (comandos 'k' y 'v') ---------------
// Todos los pines del XIAO que no usan el OLED, el buzzer ni el LED.
static const uint8_t PIN_DIAG[]      = {  1,    2,    3,    7,    8,    9,   43,   44 };
static const char*   PIN_DIAG_ETIQ[] = {"D0", "D1", "D2", "D8", "D9","D10", "D6", "D7"};
static const uint8_t PIN_DIAG_N      = sizeof(PIN_DIAG);
// Modo vigilancia: muestrea en CADA vuelta del loop (no una vez por frame)
// e imprime solo cuando un pin cambia de nivel. A diferencia de 'k', que se
// consulta desde afuera, esto no se puede perder una pulsación corta.
static bool    pinWatch = false;
static uint8_t pinWatchPrev[8];

// Vigilancia del acelerómetro: imprime el vector de gravedad filtrado.
// Sirve para descubrir qué eje del MPU corresponde a cada inclinación en el
// montaje real, en vez de deducirlo del datasheet y errarle.
static bool     imuWatch = false;
static uint32_t imuWatchProx = 0;

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

// Registra cualquier interacción: actualiza ultimaActividad y reinicia el
// contador del "¿qué pasa?" por inactividad.
static void marcarActividad(uint32_t ahora) {
    ultimaActividad = ahora;
    sigQuePasa      = ahora + INACTIVIDAD_QUEHACER_MS;
}

// ------------------------------------------------------------
static void scanI2C() {
    Serial.println("[i2c] escaneando bus...");
    // Solo rango válido 0x08..0x77; las direcciones fuera de ahí son
    // reservadas y generan falsos positivos.
    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0)
            Serial.printf("[i2c] dispositivo en 0x%02X\n", addr);
    }
}

// ------------------------------------------------------------
static void reaccionar(Expression e, uint32_t duracionMs,
                       const char* label, uint32_t ahora) {
    randExprActiva = false;
    marcarActividad(ahora);
    face.setExpression(e);
    reaccionHasta = ahora + duracionMs;
    reaccionLabel = label;
    appState      = AppState::REACTING;
}

// ------------------------------------------------------------
static void volverAlIdle(uint32_t ahora) {
    appState       = AppState::IDLE;
    idleExprActual = caraDeReposo();
    face.setExpression(idleExprActual);
    marcarActividad(ahora);
}

// ------------------------------------------------------------
// Corre al bootear y también al salir del standby: para el que mira, las dos
// situaciones son la misma —la pantalla estaba negra y vuelve—, así que
// merecen la misma entrada.
static void dispararEncendido(uint32_t ahora) {
    // Animación de encendido tipo CRT, no bloqueante, ~2.1 s en 4 fases:
    //   0: una línea horizontal crece desde el centro
    //   1: la línea se abre en vertical hasta llenar la pantalla (flash)
    //   2: estática de sintonía
    //   3: la cara se revela con un barrido descendente
    appState          = AppState::ENCENDIENDO;
    encendidoInicioMs = ahora;
    encendidoFase     = 0;
    face.setExpression(caraDeReposo());
    sound.play(Melody::TV_ON);
    randExprActiva = false;
    Serial.println("[app] encendido CRT");
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
    scheduleGuino(ahora);
    scheduleSospechoso(ahora);
    randExprActiva = false;
    // Despertar corre la misma animación de encendido que el arranque: el
    // panel estuvo apagado, así que desde afuera es indistinguible de
    // prenderlo, y aparecer de golpe con la cara ya puesta se sentía abrupto.
    dispararEncendido(ahora);
    Serial.println("[app] standby — despertando");
}

// ------------------------------------------------------------
// Clasificación de eventos del menú: un pulso de "mover cursor" puede venir
// del botón B o de la palanca; uno de "activar", del botón C o del pulsador
// de la palanca. Así los dos caminos de control conviven sin duplicar nada.
static inline bool evMueveCursor(InputEvent ev) {
    return ev == InputEvent::BTN_B_PRESS ||
           ev == InputEvent::JOY_DOWN    || ev == InputEvent::JOY_UP;
}
static inline bool evActiva(InputEvent ev) {
    return ev == InputEvent::BTN_C_PRESS || ev == InputEvent::JOY_SW_PRESS;
}

// ------------------------------------------------------------
static void despacharEventos(uint32_t ahora) {
    InputEvent ev;
    while ((ev = input.nextEvent()) != InputEvent::NONE) {

        // Standby: cualquier control despierta la pantalla
        if (appState == AppState::STANDBY) {
            salirStandby(ahora);
            continue;
        }

        // ENCENDIENDO: ignorar toda interacción durante la animación
        if (appState == AppState::ENCENDIENDO) continue;

        // ── ARCADE ───────────────────────────────────────────────
        // Todos los controles pasan al módulo del arcade, que tiene su
        // propia máquina de estados y decide cuándo salir.
        if (appState == AppState::ARCADE) {
            arcade.handleEvent(ev, ahora);
            if (arcade.quiereSalir()) {
                volverAlIdle(ahora);
                Serial.println("[app] arcade -> IDLE");
            }
            continue;
        }

        // ── MENÚ DE SISTEMA ──────────────────────────────────────
        if (appState == AppState::MENU) {
            menuHasta = ahora + MENU_TIMEOUT_MS;

            if (ev == InputEvent::BTN_A_PRESS) {
                if (menuPagina < MENU_PAGINAS) {
                    menuPagina++;
                    sound.play(Melody::BIP);
                } else {
                    volverAlIdle(ahora);
                    sound.play(Melody::BIP);
                }
            } else if (menuPagina == 1) {
                // Página de info: la única acción es instalar el update
                // pendiente, si es que hay uno.
                if (evActiva(ev) && ota.hayActualizacion()) {
                    sound.play(Melody::BIP);
                    ota.instalarAhora();   // bloqueante; reinicia si sale bien
                    volverAlIdle(ahora);   // si volvió acá, falló
                }
            } else {
                // Página de ajustes
                if (evMueveCursor(ev)) {
                    if (ev == InputEvent::JOY_UP)
                        ajustesSel = (ajustesSel + MENU_AJUSTES_OPTS - 1) % MENU_AJUSTES_OPTS;
                    else
                        ajustesSel = (ajustesSel + 1) % MENU_AJUSTES_OPTS;
                    sound.play(Melody::BIP);
                } else if (evActiva(ev)) {
                    if (ajustesSel == 0) {
                        bool nuevo = !sound.enabled();
                        sound.setEnabled(nuevo);
                        Serial.printf("[app] ajustes: sonido %s\n", nuevo ? "ON" : "OFF");
                        if (nuevo) sound.play(Melody::BIP);
                    } else {
                        volverAlIdle(ahora);
                        net.startPortal();
                        sound.play(Melody::BIP);
                        reaccionar(Expression::SORPRENDIDO, 3000,
                                   "portal: Ramoncito-setup", ahora);
                    }
                }
            }
            continue;
        }

        // ── IDLE / REACTING ──────────────────────────────────────
        // C entra al arcade, que es la función principal del aparato.
        // A abre el menú de sistema. B no tiene destino todavía: bipea,
        // para que se note que llega al firmware.
        if (evActiva(ev)) {
            marcarActividad(ahora);
            randExprActiva = false;
            appState = AppState::ARCADE;
            arcade.enter(ahora);
        } else if (ev == InputEvent::BTN_A_PRESS) {
            marcarActividad(ahora);
            randExprActiva = false;
            appState   = AppState::MENU;
            menuPagina = 1;
            ajustesSel = 0;
            menuHasta  = ahora + MENU_TIMEOUT_MS;
            sound.play(Melody::BIP);
        } else {
            marcarActividad(ahora);
            sound.play(Melody::BIP);
        }
    }
}

// ------------------------------------------------------------
static void imprimirEstado() {
    Serial.printf("[info] FW %s | estado:%d | hora:%d horaValida:%d portal:%d | sonido:%d | heap:%lu\n",
                  FW_VERSION, (int)appState,
                  net.hourNow(), net.timeValid(), net.portalActive(),
                  sound.enabled(), (unsigned long)ESP.getFreeHeap());
    Serial.printf("[joy] %s | X:%u(c%u)=%.2f Y:%u(c%u)=%.2f | btn A:%d B:%d C:%d SW:%d\n",
                  input.joystickPresente() ? "hw" : "stub",
                  input.rawX(), input.centroX(), input.axisX(),
                  input.rawY(), input.centroY(), input.axisY(),
                  input.btnA(), input.btnB(), input.btnC(), input.joySw());
}

static void procesarComando(const char* cmd) {
    if (cmd[0] == 'h') {
        int h = atoi(cmd + 1);
        net.forceHour(h);
        Serial.printf("[cmd] hora forzada: %d\n", h);
    } else if (cmd[0] == 's') {
        sound.setEnabled(!sound.enabled());
        Serial.printf("[cmd] sonido: %d\n", sound.enabled());
    } else if (cmd[0] == 'p') {
        net.startPortal();
        Serial.println("[cmd] portal forzado");
    } else if (cmd[0] == 'i') {
        imprimirEstado();
    } else if (cmd[0] == 'n') {
        if (appState == AppState::MENU) {
            volverAlIdle(millis());
        } else {
            appState   = AppState::MENU;
            menuPagina = 1;
            ajustesSel = 0;
            menuHasta  = millis() + MENU_TIMEOUT_MS;
        }
        Serial.printf("[cmd] menu: %d\n", appState == AppState::MENU);
    } else if (cmd[0] == 'u') {
        ota.forzarChequeo();
        Serial.println("[cmd] chequeo OTA forzado");
    } else if (cmd[0] == 'k') {
        // Escaneo de pines: pone en INPUT_PULLUP todos los pines libres del
        // XIAO e imprime el nivel de cada uno. Sirve para descubrir en qué
        // GPIO está cableado un botón sin seguir el cable a ojo: el que
        // aparezca en 0 mientras se lo mantiene apretado es ese.
        // Se saltean los ejes de la palanca si está montada, para no pisar
        // el ADC con un pull-up.
        char linea[192];
        int n = snprintf(linea, sizeof(linea), "[scan]");
        for (uint8_t i = 0; i < PIN_DIAG_N && n > 0 && n < (int)sizeof(linea); i++) {
            if (JOYSTICK_PRESENTE && (PIN_DIAG[i] == PIN_JOY_X || PIN_DIAG[i] == PIN_JOY_Y)) continue;
            pinMode(PIN_DIAG[i], INPUT_PULLUP);
            n += snprintf(linea + n, sizeof(linea) - n, " %s/g%u:%d",
                          PIN_DIAG_ETIQ[i], PIN_DIAG[i], digitalRead(PIN_DIAG[i]));
        }
        Serial.println(linea);
    } else if (cmd[0] == 'v') {
        // Vigilancia on/off. Con esto no hace falta acertarle al momento de
        // la pulsación: cualquier flanco en cualquier pin queda impreso.
        pinWatch = !pinWatch;
        if (pinWatch) {
            for (uint8_t i = 0; i < PIN_DIAG_N; i++) {
                if (JOYSTICK_PRESENTE && (PIN_DIAG[i] == PIN_JOY_X || PIN_DIAG[i] == PIN_JOY_Y)) continue;
                pinMode(PIN_DIAG[i], INPUT_PULLUP);
                pinWatchPrev[i] = (uint8_t)digitalRead(PIN_DIAG[i]);
            }
            Serial.println("[watch] ON — vigilando todos los pines libres, apreta lo que quieras");
        } else {
            Serial.println("[watch] OFF");
        }
    } else if (cmd[0] == 'e') {
        // Pasa por todas las expresiones, una por vez. Sin esto habría que
        // esperar a que salgan solas —algunas cada varios minutos— para ver
        // cómo quedó cada boca.
        static const char* NOMBRES[] = {
            "NEUTRAL", "FELIZ", "TRISTE", "ENOJADO", "SORPRENDIDO",
            "ABURRIDO", "DORMIDO", "SOSPECHOSO", "AMOR", "GUINO",
            "RISA", "MAREADO", "ILUSIONADO"
        };
        static uint8_t demo = 0;
        demo = (uint8_t)((demo + 1) % 13);
        Serial.printf("[cara] %u/13 %s\n", demo + 1, NOMBRES[demo]);
        reaccionar((Expression)demo, 20000, NOMBRES[demo], millis());
    } else if (cmd[0] == 'o') {
        // Fuerza el standby. Sin esto habría que esperar los 10 minutos de
        // inactividad para probar cómo despierta.
        entrarStandby();
    } else if (cmd[0] == 'g') {
        imuWatch = !imuWatch;
        Serial.printf("[imu] vigilancia %s\n", imuWatch ? "ON - inclina el aparato" : "OFF");
    } else if (cmd[0] == 'w' || cmd[0] == 'x' || cmd[0] == 'a' || cmd[0] == 'd') {
        // Stub de la palanca mientras no esté el módulo físico: cada tecla
        // es un golpe que vuelve solo al centro (ver Input::setStubAxis).
        uint32_t ahora = millis();
        switch (cmd[0]) {
            case 'w': input.setStubAxis( 0.0f, -1.0f, ahora); break;  // arriba
            case 'x': input.setStubAxis( 0.0f,  1.0f, ahora); break;  // abajo
            case 'a': input.setStubAxis(-1.0f,  0.0f, ahora); break;  // izquierda
            case 'd': input.setStubAxis( 1.0f,  0.0f, ahora); break;  // derecha
        }
        Serial.printf("[cmd] palanca stub: %c\n", cmd[0]);
    } else if (cmd[0] == '1' || cmd[0] == '2' || cmd[0] == '3' || cmd[0] == 'z') {
        InputEvent ev = (cmd[0] == '1') ? InputEvent::BTN_A_PRESS  :
                        (cmd[0] == '2') ? InputEvent::BTN_B_PRESS  :
                        (cmd[0] == '3') ? InputEvent::BTN_C_PRESS  :
                                          InputEvent::JOY_SW_PRESS;
        input.injectEvent(ev);
        Serial.printf("[cmd] boton simulado: %c\n", cmd[0]);
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
    Serial.printf ("  Ramoncito arcade | FW: %s\n", FW_VERSION);
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
    input.begin();
    arcade.begin();
    face.begin();
    imu.begin();   // requiere Wire ya iniciado; falla silenciosamente si no hay MPU
    net.begin();
    ota.begin(&u8g2);

    uint32_t ahora = millis();
    marcarActividad(ahora);
    scheduleGuino(ahora);
    scheduleSospechoso(ahora);
    dispararEncendido(ahora);

    Serial.println("[app] listo — comandos: h N | s | p | i | n | u | k | v");
    Serial.println("[app] test de controles: 1/2/3 = botones A/B/C | z = pulsador palanca | w/x/a/d = palanca");
}

// ------------------------------------------------------------
void loop() {
    uint32_t ahora = millis();

    input.poll(ahora);
    leerSerial();

    // Vigilancia de pines: corre antes del gate de frame, así muestrea a la
    // velocidad del loop y no se le escapa una pulsación corta.
    if (pinWatch) {
        for (uint8_t i = 0; i < PIN_DIAG_N; i++) {
            if (JOYSTICK_PRESENTE && (PIN_DIAG[i] == PIN_JOY_X || PIN_DIAG[i] == PIN_JOY_Y)) continue;
            uint8_t nivel = (uint8_t)digitalRead(PIN_DIAG[i]);
            if (nivel != pinWatchPrev[i]) {
                pinWatchPrev[i] = nivel;
                Serial.printf("[watch] %s/g%u -> %s\n",
                              PIN_DIAG_ETIQ[i], PIN_DIAG[i],
                              nivel ? "SUELTO (1)" : "APRETADO (0)");
            }
        }
    }

    if (ahora - ultimoFrame < FRAME_MS) return;
    ultimoFrame = ahora;
    framesEnVentana++;

    // Módulos de fondo
    imu.update(ahora);

    if (imuWatch && (int32_t)(ahora - imuWatchProx) >= 0) {
        imuWatchProx = ahora + 200;
        Serial.printf("[grav] X:%+.3f Y:%+.3f Z:%+.3f\n",
                      imu.gravX(), imu.gravY(), imu.gravZ());
    }
    net.update(ahora);
    sound.update(ahora);

    // Auto-OTA: solo fuera del menú y del arcade. El chequeo es bloqueante y
    // pisaría el render con la pantalla de progreso; en medio de una partida
    // eso es un frame congelado de varios segundos.
    if (appState != AppState::MENU && appState != AppState::ARCADE) {
        ota.update(ahora);
    }

    despacharEventos(ahora);

    // ── Arcade: física del juego y cierre por inactividad ───────
    if (appState == AppState::ARCADE) {
        arcade.update(ahora);
        marcarActividad(ahora);   // jugando no se cuenta como estar inactivo
        if (arcade.quiereSalir()) volverAlIdle(ahora);
    }

    // ── Eventos del IMU (acelerómetro) ──────────────────────────
    // Solo fuera del menú y del arcade: sacudir el gabinete jugando al Pong
    // no debería cambiarte la pantalla.
    if (appState != AppState::MENU && appState != AppState::ARCADE &&
        appState != AppState::ENCENDIENDO && imu.habilitado()) {

        if (imu.huboLevantado()) {
            marcarActividad(ahora);
            if (appState == AppState::STANDBY) {
                salirStandby(ahora);
            } else {
                sound.play(Melody::FELIZ);
                reaccionar(Expression::ILUSIONADO, REACCION_TOUCH_MS, "", ahora);
                Serial.println("[imu] levantado → ILUSIONADO");
            }
        }

        if (imu.huboSacudida()) {
            marcarActividad(ahora);
            if (appState == AppState::STANDBY) {
                salirStandby(ahora);
            } else if (imu.sacudidasEnVentana() >= IMU_SACUDIDA_MAX) {
                // Lo sacudieron de más: se marea. Es una reacción del momento,
                // no un enojo que quede pegado — no hay forma de pedirle
                // perdón desde que no hay caricias.
                sound.play(Melody::ENOJADO);
                reaccionar(Expression::MAREADO, REACCION_BTN_MS, "", ahora);
                Serial.printf("[imu] sacudida excesiva (%u) → MAREADO\n",
                              imu.sacudidasEnVentana());
            } else {
                sound.play(Melody::BIP);
                reaccionar(Expression::SORPRENDIDO, REACCION_TOUCH_MS, "", ahora);
                Serial.printf("[imu] sacudida leve (%u) → SORPRENDIDO\n",
                              imu.sacudidasEnVentana());
            }
        }
    }

    // Standby por inactividad prolongada
    if (appState == AppState::IDLE &&
        (ahora - ultimaActividad) >= INACTIVIDAD_STANDBY_MS) {
        entrarStandby();
    }
    if (appState == AppState::STANDBY) return;

    // Auto-cierre del menú
    if (appState == AppState::MENU && (int32_t)(ahora - menuHasta) >= 0) {
        menuPagina = 1;
        volverAlIdle(ahora);
    }

    // ── Animación de encendido CRT ───────────────────────────────
    if (appState == AppState::ENCENDIENDO) {
        uint32_t t  = ahora - encendidoInicioMs;
        uint32_t t1 = ANIM_NACIMIENTO_F0_MS;
        uint32_t t2 = t1 + ANIM_NACIMIENTO_F1_MS;
        uint32_t t3 = t2 + ANIM_NACIMIENTO_F2_MS;

        if (encendidoFase == 0 && t >= t1)                          encendidoFase = 1;
        else if (encendidoFase == 1 && t >= t2)                      encendidoFase = 2;
        else if (encendidoFase == 2 && t >= t3)                      encendidoFase = 3;
        else if (encendidoFase == 3 && t >= ANIM_NACIMIENTO_TOTAL_MS) {
            volverAlIdle(ahora);
            scheduleGuino(ahora);
            scheduleSospechoso(ahora);
            Serial.println("[app] encendido CRT completo → IDLE");
        }

        if (appState == AppState::ENCENDIENDO) {
            // Render propio: durante las fases 0-2 no hay cara que dibujar.
            u8g2.clearBuffer();

            if (encendidoFase == 0) {
                // Fase 0: una línea horizontal crece desde el centro
                float p = (float)t / (float)ANIM_NACIMIENTO_F0_MS;
                if (p > 1.0f) p = 1.0f;
                int16_t lineaW = (int16_t)(2.0f + p * (128.0f - 2.0f));
                int16_t lineaX = (128 - lineaW) / 2;
                int16_t lineaY = 32 - (ANIM_NACIMIENTO_LINEA_GROSOR / 2);
                u8g2.setDrawColor(1);
                u8g2.drawBox(lineaX, lineaY, lineaW, ANIM_NACIMIENTO_LINEA_GROSOR);

            } else if (encendidoFase == 1) {
                // Fase 1: la línea se abre en vertical hasta llenar la pantalla
                float p = (float)(t - t1) / (float)ANIM_NACIMIENTO_F1_MS;
                if (p > 1.0f) p = 1.0f;
                int16_t altBase = ANIM_NACIMIENTO_LINEA_GROSOR;
                int16_t alt = (int16_t)(altBase + p * (64 - altBase));
                int16_t rectY = 32 - alt / 2;
                if (rectY < 0) rectY = 0;
                u8g2.setDrawColor(1);
                u8g2.drawBox(0, rectY, 128, alt);

            } else if (encendidoFase == 2) {
                // Fase 2: estática de sintonía
                u8g2.setDrawColor(1);
                for (uint16_t i = 0; i < ANIM_NACIMIENTO_RUIDO_PX; i++) {
                    u8g2.drawPixel((int16_t)random(128), (int16_t)random(64));
                }
                for (uint8_t fila = 4; fila < 64; fila += 8) {
                    if (random(3) == 0) u8g2.drawHLine(0, fila, 128);
                }

            } else {
                // Fase 3: la cara se revela con un barrido descendente
                face.update(ahora);
                face.render(u8g2);
                float p = (float)(t - t3) / (float)ANIM_NACIMIENTO_F3_MS;
                if (p > 1.0f) p = 1.0f;
                int16_t reveladoY = (int16_t)(p * 64.0f);
                if (reveladoY < 64) {
                    u8g2.setDrawColor(0);
                    u8g2.drawBox(0, reveladoY, 128, 64 - reveladoY);
                }
                u8g2.setDrawColor(1);
            }

            u8g2.sendBuffer();
            return;
        }
    }

    // Fin de reacción
    if (appState == AppState::REACTING &&
        (int32_t)(ahora - reaccionHasta) >= 0) {
        reaccionLabel = "";
        volverAlIdle(ahora);
        scheduleGuino(ahora);
        scheduleSospechoso(ahora);
    }

    // ── Gestos ocasionales durante el idle ───────────────────────
    // Es de acá que sale la vida de la carita ahora que no hay humor:
    // guiños cada tanto y una mirada de "¿qué pasa?" si nadie la usa.
    if (appState == AppState::IDLE) {
        if (randExprActiva) {
            if ((int32_t)(ahora - randExprHasta) >= 0) {
                face.setExpression(idleExprActual);
                randExprActiva = false;
            }
        } else {
            bool quePasaDisparar = (sigQuePasa != 0) && ((int32_t)(ahora - sigQuePasa) >= 0);

            if ((int32_t)(ahora - sigGuino) >= 0) {
                randExprActiva = true;
                randExprHasta  = ahora + RAND_EXPR_DUR_MS;
                face.setExpression(Expression::GUINO);
                scheduleGuino(ahora + RAND_EXPR_DUR_MS);
            } else if (quePasaDisparar || (int32_t)(ahora - sigSospechoso) >= 0) {
                randExprActiva = true;
                uint32_t dur = quePasaDisparar ? QUEHACER_EXPR_DUR_MS : RAND_EXPR_DUR_MS;
                randExprHasta = ahora + dur;
                face.setExpression(Expression::SOSPECHOSO);
                if (quePasaDisparar) sigQuePasa += INACTIVIDAD_QUEHACER_MS;
                else                 scheduleSospechoso(ahora + dur);
            } else if (idleExprActual != caraDeReposo()) {
                idleExprActual = caraDeReposo();
                face.setExpression(idleExprActual);
            }
        }
    }

    // Animar la cara
    face.update(ahora);

    // ── Render ───────────────────────────────────────────────────
    u8g2.clearBuffer();
    if (appState == AppState::MENU) {
        MenuData md;
        md.horaValida = net.timeValid();
        md.hora = md.minuto = 0;
        md.dia  = md.mes = md.diaSemana = 0;
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
        md.staConectada    = net.staConnected();
        // Buffer local: vive hasta que menuRender() retorna (mismo scope).
        char lanIpBuf[20];
        snprintf(lanIpBuf, sizeof(lanIpBuf), "%s", net.lanIP().c_str());
        md.lanIP           = lanIpBuf;
        md.fwVersion       = FW_VERSION;
        md.hayUpdate       = ota.hayActualizacion();
        md.versionNueva    = ota.versionNueva();
        md.sonidoHabilitado= sound.enabled();
        md.ajustesSel      = ajustesSel;
        menuRender(u8g2, md, menuPagina);
    } else if (appState == AppState::ARCADE) {
        arcade.render(u8g2);
    } else {
        face.render(u8g2);
        if (appState == AppState::REACTING && reaccionLabel[0] != '\0') {
            u8g2.setFont(u8g2_font_5x7_tf);
            u8g2.drawStr(2, 8, reaccionLabel);
        }
    }
    u8g2.sendBuffer();

    // Log 1/s
    if (ahora - ultimoLog >= INTERVALO_LOG_MS) {
        ultimoLog = ahora;
        fpsActual = framesEnVentana * 1000 / INTERVALO_LOG_MS;
        framesEnVentana = 0;
        Serial.printf("Ramoncito | fps:%lu heap:%lu | hora:%d | est:%d | joy:%.2f,%.2f\n",
                      (unsigned long)fpsActual, (unsigned long)ESP.getFreeHeap(),
                      net.hourNow(), (int)appState,
                      input.axisX(), input.axisY());
    }
}
