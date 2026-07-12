// =============================================================
//  espToy — Etapa 0: Hola Mundo
//  Plataforma: Seeed Studio XIAO ESP32-S3
//  Descripción: arranque mínimo, parpadeo de LED y reporte de
//               heap por serial. Sirve de base para las etapas
//               siguientes y verifica que el entorno compila.
// =============================================================

#include <Arduino.h>

// ----- Constantes de hardware --------------------------------
// En el XIAO ESP32-S3 el LED incorporado es GPIO21 y es activo
// en BAJO: LOW = encendido, HIGH = apagado.
static const uint8_t  PIN_LED            = 21;

// ----- Constantes de temporización --------------------------
static const uint32_t INTERVALO_LED_MS   = 500;   // ms entre cambios de LED
static const uint32_t INTERVALO_TICK_MS  = 1000;  // ms entre prints de tick

// ----- Versión de firmware ----------------------------------
static const char* FW_VERSION = "0.1.0-etapa0";

// ----- Variables de estado ----------------------------------
static uint32_t ultimoLed  = 0;
static uint32_t ultimoTick = 0;
static bool     ledEncendido = false;
static uint32_t contadorTick = 0;

// ------------------------------------------------------------
void setup() {
    // Iniciar comunicación serial (USB CDC en ESP32-S3)
    Serial.begin(115200);

    // Dar tiempo al host para enumerar el puerto USB CDC
    delay(1500);

    Serial.println("=========================================");
    Serial.print  ("  espToy boot OK — Etapa 0  |  FW: ");
    Serial.println(FW_VERSION);
    Serial.println("=========================================");

    // Configurar LED incorporado como salida y apagarlo
    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);  // HIGH = apagado (activo en BAJO)
}

// ------------------------------------------------------------
void loop() {
    uint32_t ahora = millis();

    // --- Parpadeo de LED cada INTERVALO_LED_MS ---------------
    if (ahora - ultimoLed >= INTERVALO_LED_MS) {
        ultimoLed = ahora;
        ledEncendido = !ledEncendido;
        // Activo en BAJO: encendido → LOW, apagado → HIGH
        digitalWrite(PIN_LED, ledEncendido ? LOW : HIGH);
    }

    // --- Reporte de estado cada INTERVALO_TICK_MS ------------
    if (ahora - ultimoTick >= INTERVALO_TICK_MS) {
        ultimoTick = ahora;
        contadorTick++;

        uint32_t uptimeSeg  = ahora / 1000;
        uint32_t heapLibre  = ESP.getFreeHeap();

        Serial.print("espToy tick ");
        Serial.print(contadorTick);
        Serial.print(" — uptime ");
        Serial.print(uptimeSeg);
        Serial.print("s — heap libre: ");
        Serial.println(heapLibre);
    }
}
