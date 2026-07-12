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

// ----- Display -----------------------------------------------
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ----- Máquina de estados global ------------------------------
enum class AppState : uint8_t { IDLE, REACTING, SLEEPING };
static AppState appState = AppState::IDLE;

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

        switch (ev) {
            case InputEvent::BTN_A_PRESS:
                mood.apply(MoodEffect::BTN_A);
                sound.play(Melody::SORPRESA);
                reaccionar(Expression::SORPRENDIDO, REACCION_BTN_MS, "BTN A (D0/GPIO1)");
                break;
            case InputEvent::BTN_B_PRESS:
                mood.apply(MoodEffect::BTN_B);
                sound.play(Melody::FELIZ);
                reaccionar(Expression::FELIZ, REACCION_BTN_MS, "BTN B (D1/GPIO2)");
                break;
            case InputEvent::TOUCH_START:
                mood.apply(MoodEffect::CARICIA);
                sound.play(Melody::AMOR);
                reaccionar(Expression::AMOR, REACCION_TOUCH_MS, "CARICIA (D2/GPIO3)", true);
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

    if (ahora - ultimoLed >= 500) {
        ultimoLed = ahora;
        ledEncendido = !ledEncendido;
        digitalWrite(PIN_LED, ledEncendido ? LOW : HIGH);
    }

    if (ahora - ultimoFrame < FRAME_MS) return;
    ultimoFrame = ahora;
    framesEnVentana++;

    // Módulos de fondo
    net.update(ahora);
    mood.update(ahora);
    sound.update(ahora);

    // Hora recién validada -> decaimiento offline (una sola vez)
    if (net.justGotValidTime()) {
        time_t nowEpoch = time(nullptr);
        mood.applyOfflineDecay(nowEpoch);
        mood.noteTimeValid(nowEpoch);
    }

    despacharEventos();

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

    // Render
    u8g2.clearBuffer();
    face.render(u8g2);
    if (appState == AppState::REACTING && reaccionLabel[0] != '\0') {
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawStr(2, 63, reaccionLabel);
    }
    if (net.portalActive()) {
        u8g2.setFont(u8g2_font_4x6_tf);
        u8g2.drawStr(2, 6, "WiFi: espToy-setup");
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
