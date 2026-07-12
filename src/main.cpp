// =============================================================
//  espToy — Etapas 2+3: Motor de ojos + Entradas
//  Plataforma: Seeed Studio XIAO ESP32-S3
//  main.cpp = orquestador: máquina de estados, despacho de
//  eventos de input a reacciones de la cara, render y logs.
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "config.h"
#include "face.h"
#include "input.h"

// ----- Display -----------------------------------------------
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ----- Máquina de estados global ------------------------------
enum class AppState : uint8_t { IDLE, REACTING };
static AppState appState = AppState::IDLE;

// Datos de la reacción en curso
static uint32_t    reaccionHasta = 0;      // millis() en que termina
static const char* reaccionLabel = "";     // texto overlay (identificar botones)
static bool        reaccionEsTouch = false; // la caricia dura mientras toques

// ----- Estado varios ------------------------------------------
static uint32_t ultimoLed = 0, ultimoLog = 0, ultimoFrame = 0;
static bool     ledEncendido = false;
static uint32_t framesEnVentana = 0, fpsActual = 0;

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
// Inicia una reacción: expresión + duración + etiqueta en pantalla
static void reaccionar(Expression e, uint32_t duracionMs,
                       const char* label, bool esTouch = false) {
    face.setExpression(e);
    reaccionHasta   = millis() + duracionMs;
    reaccionLabel   = label;
    reaccionEsTouch = esTouch;
    appState        = AppState::REACTING;
}

// ------------------------------------------------------------
// Despacha los eventos de la cola de input según el estado
static void despacharEventos() {
    InputEvent ev;
    while ((ev = input.nextEvent()) != InputEvent::NONE) {
        switch (ev) {
            case InputEvent::BTN_A_PRESS:
                reaccionar(Expression::SORPRENDIDO, REACCION_BTN_MS, "BTN A (D0/GPIO1)");
                break;
            case InputEvent::BTN_B_PRESS:
                reaccionar(Expression::FELIZ, REACCION_BTN_MS, "BTN B (D1/GPIO2)");
                break;
            case InputEvent::TOUCH_START:
                reaccionar(Expression::AMOR, REACCION_TOUCH_MS, "CARICIA (D2/GPIO3)", true);
                break;
            case InputEvent::TOUCH_END:
                // la reacción termina sola cuando venza el timer
                break;
            default:
                break;
        }
    }
}

// ------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1500);  // enumeración USB CDC

    Serial.println("=========================================");
    Serial.printf ("  espToy boot OK — Etapas 2+3  |  FW: %s\n", FW_VERSION);
    Serial.println("=========================================");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);

    Wire.begin();
    Wire.setClock(I2C_CLOCK_HZ);
    scanI2C();

    if (!u8g2.begin()) Serial.println("[oled] ERROR: begin() fallo");
    else               Serial.println("[oled] SSD1309 OK");
    u8g2.setBusClock(I2C_CLOCK_HZ);

    input.begin();   // incluye autocalibración del touch (~0.5 s)
    face.begin();

    Serial.println("[app] listo — proba los botones y el touch");
}

// ------------------------------------------------------------
void loop() {
    uint32_t ahora = millis();

    // Entradas a máxima frecuencia (debounce fino)
    input.poll(ahora);

    // LED de vida
    if (ahora - ultimoLed >= 500) {
        ultimoLed = ahora;
        ledEncendido = !ledEncendido;
        digitalWrite(PIN_LED, ledEncendido ? LOW : HIGH);
    }

    // Frame a ~30 fps
    if (ahora - ultimoFrame < FRAME_MS) return;
    ultimoFrame = ahora;
    framesEnVentana++;

    despacharEventos();

    // Fin de reacción → volver a idle (la caricia se extiende mientras toques)
    if (appState == AppState::REACTING) {
        bool vencida = (int32_t)(ahora - reaccionHasta) >= 0;
        if (reaccionEsTouch && input.touching())
            reaccionHasta = ahora + 500;   // sigue mientras haya contacto
        else if (vencida) {
            face.setExpression(Expression::NEUTRAL);
            appState = AppState::IDLE;
            reaccionLabel = "";
        }
    }

    face.update(ahora);

    u8g2.clearBuffer();
    face.render(u8g2);
    if (appState == AppState::REACTING && reaccionLabel[0] != '\0') {
        // Overlay: qué entrada disparó la reacción (para identificar botones)
        u8g2.setFont(u8g2_font_5x7_tf);
        u8g2.drawStr(2, 63, reaccionLabel);
    }
    u8g2.sendBuffer();

    // Log de estado 1/s
    if (ahora - ultimoLog >= INTERVALO_LOG_MS) {
        ultimoLog = ahora;
        fpsActual = framesEnVentana * 1000 / INTERVALO_LOG_MS;
        framesEnVentana = 0;
        Serial.printf("espToy | fps:%lu heap:%lu | A:%d B:%d | touch:%lu (base:%lu)\n",
                      (unsigned long)fpsActual,
                      (unsigned long)ESP.getFreeHeap(),
                      input.btnA(), input.btnB(),
                      (unsigned long)input.touchValue(),
                      (unsigned long)input.touchBaseline());
    }
}
