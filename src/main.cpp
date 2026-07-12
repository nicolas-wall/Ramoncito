// =============================================================
//  espToy — Etapas 4+5+6: Sonido + Cerebro Tamagotchi + Día/Noche
//  main.cpp = orquestador: máquina de estados global, despacho
//  de eventos, integración mood/sound/net y comandos de test.
//
//  Comandos por serial (terminados en Enter):
//    h N     fuerza la hora a N (0-23) para probar la noche; h -1 la libera
//    m F E A fija humor: felicidad energía aburrimiento (0-100)
//    s       alterna sonido on/off
//    p       fuerza el portal de configuración WiFi
//    i       imprime estado completo
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
#include "net.h"
#include "menu.h"
#include "pong.h"

// ----- Display -----------------------------------------------
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ----- Máquina de estados global ------------------------------
enum class AppState : uint8_t { IDLE, REACTING, SLEEPING, MENU, PONG };
static AppState appState = AppState::IDLE;

static uint32_t menuHasta = 0;                 // auto-cierre del menú
static const uint32_t MENU_TIMEOUT_MS = 10000;

static uint32_t    reaccionHasta   = 0;
static const char* reaccionLabel   = "";
static bool        reaccionEsTouch = false;

static Expression  idleExprActual  = Expression::NEUTRAL;
static uint32_t    proximoRonquido = 0;

// ----- Estado varios ------------------------------------------
static uint32_t ultimoLed = 0, ultimoLog = 0, ultimoFrame = 0;
static bool     ledEncendido = false;
static uint32_t framesEnVentana = 0, fpsActual = 0;
static char     cmdBuf[32];
static uint8_t  cmdLen = 0;

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
                       const char* label, bool esTouch = false) {
    face.setExpression(e);
    reaccionHasta   = millis() + duracionMs;
    reaccionLabel   = label;
    reaccionEsTouch = esTouch;
    appState        = AppState::REACTING;
}

// ------------------------------------------------------------
static void entrarADormir(uint32_t ahora) {
    appState = AppState::SLEEPING;
    face.setExpression(Expression::DORMIDO);
    sound.play(Melody::DORMIR);
    proximoRonquido = ahora + 3000;
    Serial.println("[app] a dormir...");
}

static void despertar() {
    appState = AppState::IDLE;
    idleExprActual = mood.dominantExpression();
    face.setExpression(idleExprActual);
    sound.play(Melody::DESPERTAR);
    Serial.println("[app] buen dia!");
}

// ------------------------------------------------------------
static void despacharEventos() {
    InputEvent ev;
    while ((ev = input.nextEvent()) != InputEvent::NONE) {

        // Dormido: cualquier botón lo despierta de mal humor y vuelve a dormir
        if (appState == AppState::SLEEPING) {
            if (ev == InputEvent::BTN_A_PRESS || ev == InputEvent::BTN_B_PRESS) {
                mood.apply(MoodEffect::DESPERTADO_DE_NOCHE);
                sound.play(Melody::ENOJADO);
                reaccionar(Expression::ENOJADO, REACCION_ENOJO_NOCHE_MS, "GRRR... dejame dormir");
            }
            continue;  // el touch no lo despierta (caricia mientras duerme, ok)
        }

        // Combo secreto: entra al Pong desde IDLE/MENU/REACTING
        // (de noche no — los botones ya dispararon la protesta)
        if (ev == InputEvent::COMBO_AB_3S) {
            if (appState == AppState::PONG) {
                // Mismo combo para abandonar la partida
                pong.exit();
                appState = AppState::IDLE;
                idleExprActual = mood.dominantExpression();
                face.setExpression(idleExprActual);
            } else if (appState != AppState::SLEEPING) {
                sound.play(Melody::SORPRESA);
                pong.enter();
                appState = AppState::PONG;
                Serial.println("[app] PONG! (combo secreto)");
            }
            continue;
        }

        // Durante el Pong los botones mueven la paleta (estado crudo),
        // los eventos de presión se ignoran
        if (appState == AppState::PONG) continue;

        // En el menú: cualquier botón lo cierra
        if (appState == AppState::MENU) {
            if (ev == InputEvent::BTN_A_PRESS || ev == InputEvent::BTN_B_PRESS) {
                appState = AppState::IDLE;
                idleExprActual = mood.dominantExpression();
                face.setExpression(idleExprActual);
                sound.play(Melody::BIP);
            }
            continue;  // touch ignorado dentro del menú
        }

        switch (ev) {
            // Botones = utilitarios: abren el menú de estado.
            // (La interacción afectiva es la caricia; combo A+B queda
            //  reservado para el juego oculto.)
            case InputEvent::BTN_A_PRESS:
            case InputEvent::BTN_B_PRESS:
                appState  = AppState::MENU;
                menuHasta = millis() + MENU_TIMEOUT_MS;
                sound.play(Melody::BIP);
                break;
            case InputEvent::TOUCH_START:
                mood.apply(MoodEffect::CARICIA);
                sound.play(Melody::AMOR);
                reaccionar(Expression::AMOR, REACCION_TOUCH_MS, "", true);
                break;
            default:
                break;
        }
    }
}

// ------------------------------------------------------------
static void imprimirEstado() {
    Serial.printf("[info] FW %s | estado:%d | F:%u E:%u A:%u | hora:%d noche:%d horaValida:%d portal:%d | sonido:%d\n",
                  FW_VERSION, (int)appState,
                  mood.happiness(), mood.energy(), mood.boredom(),
                  net.hourNow(), net.isNight(), net.timeValid(), net.portalActive(),
                  sound.enabled());
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
    } else if (cmd[0] == 'g') {
        // Entrar/salir del Pong (test sin botones físicos)
        if (appState == AppState::PONG) {
            pong.exit();
            appState = AppState::IDLE;
            face.setExpression(mood.dominantExpression());
        } else {
            pong.enter();
            appState = AppState::PONG;
        }
        Serial.printf("[cmd] pong: %d\n", appState == AppState::PONG);
    } else if (cmd[0] == 'n') {
        // Alternar menú (equivale a apretar un botón)
        if (appState == AppState::MENU) {
            appState = AppState::IDLE;
            face.setExpression(mood.dominantExpression());
        } else {
            appState  = AppState::MENU;
            menuHasta = millis() + MENU_TIMEOUT_MS;
        }
        Serial.printf("[cmd] menu: %d\n", appState == AppState::MENU);
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
    Serial.printf ("  espToy boot OK — Etapas 4+5+6 | FW: %s\n", FW_VERSION);
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
    input.begin();
    face.begin();
    net.begin();

    idleExprActual = mood.dominantExpression();
    face.setExpression(idleExprActual);
    sound.play(Melody::BOOT);

    Serial.println("[app] listo — comandos: h N | m F E A | s | p | i");
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
    // Descansando = sueño nocturno o siesta por agotamiento (cara DORMIDO):
    // en vez de decaer, recupera energía de a poco.
    bool descansando = (appState == AppState::SLEEPING) ||
                       (appState == AppState::IDLE && idleExprActual == Expression::DORMIDO);
    mood.update(ahora, descansando);
    sound.update(ahora);

    // Hora recién validada -> decaimiento offline (una sola vez)
    if (net.justGotValidTime()) {
        time_t nowEpoch = time(nullptr);
        mood.applyOfflineDecay(nowEpoch);
        mood.noteTimeValid(nowEpoch);
    }

    despacharEventos();

    // Pong: lógica del juego y fin de partida
    if (appState == AppState::PONG) {
        pong.update(ahora, input.btnA(), input.btnB());
        if (pong.done()) {
            PongResult r = pong.result();
            pong.exit();
            if (r == PongResult::GANA_JUGADOR) {
                // La mascota perdió: puchero/enojo
                mood.apply(MoodEffect::JUGO_PONG_GANO_HUMANO);
                sound.play(Melody::TRISTE);
                reaccionar(Expression::ENOJADO, 3000, "");
            } else {
                // La mascota ganó: festeja burlona
                mood.apply(MoodEffect::JUGO_PONG_GANO_CPU);
                sound.play(Melody::FELIZ);
                reaccionar(Expression::FELIZ, 3000, "");
            }
        }
    }

    // Auto-cierre del menú
    if (appState == AppState::MENU && (int32_t)(ahora - menuHasta) >= 0) {
        appState = AppState::IDLE;
        idleExprActual = mood.dominantExpression();
        face.setExpression(idleExprActual);
    }

    // Transiciones dependientes de la hora
    bool esNoche = net.isNight();
    if (appState == AppState::IDLE && esNoche)       entrarADormir(ahora);
    else if (appState == AppState::SLEEPING && !esNoche) despertar();

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
            }
        }
    }

    // En idle, si el humor cambió de categoría, actualizar la cara
    if (appState == AppState::IDLE) {
        Expression dominante = mood.dominantExpression();
        if (dominante != idleExprActual) {
            idleExprActual = dominante;
            face.setExpression(dominante);
        }
    }

    // Ronquido periódico durmiendo
    if (appState == AppState::SLEEPING && (int32_t)(ahora - proximoRonquido) >= 0) {
        sound.play(Melody::RONQUIDO);
        proximoRonquido = ahora + 2500 + (esp_random() % 1000);
    }

    // Animar la cara (interpolación, parpadeo, mirada, respiración)
    face.update(ahora);

    // Render
    u8g2.clearBuffer();
    if (appState == AppState::PONG) {
        pong.render(u8g2);
    } else if (appState == AppState::MENU) {
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
        menuRender(u8g2, md);
    } else {
        face.render(u8g2);
        if (appState == AppState::REACTING && reaccionLabel[0] != '\0') {
            u8g2.setFont(u8g2_font_5x7_tf);
            u8g2.drawStr(2, 63, reaccionLabel);
        }
        if (net.portalActive()) {
            u8g2.setFont(u8g2_font_4x6_tf);
            u8g2.drawStr(2, 6, "WiFi: espToy-setup");
        }
    }
    u8g2.sendBuffer();

    // Log 1/s
    if (ahora - ultimoLog >= INTERVALO_LOG_MS) {
        ultimoLog = ahora;
        fpsActual = framesEnVentana * 1000 / INTERVALO_LOG_MS;
        framesEnVentana = 0;
        Serial.printf("espToy | fps:%lu heap:%lu | F:%u E:%u A:%u | hora:%d noche:%d | est:%d | touch:%lu/%lu\n",
                      (unsigned long)fpsActual, (unsigned long)ESP.getFreeHeap(),
                      mood.happiness(), mood.energy(), mood.boredom(),
                      net.hourNow(), esNoche, (int)appState,
                      (unsigned long)input.touchValue(), (unsigned long)input.touchBaseline());
    }
}
