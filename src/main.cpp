// =============================================================
//  espToy — Etapa 1: Pantalla viva
//  Plataforma: Seeed Studio XIAO ESP32-S3
//  Descripción: scan I2C al boot, inicialización del OLED
//               SSD1309 128x64 con U8g2, animación de prueba
//               y medición de FPS real por serial.
// =============================================================

#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>

// ----- Constantes de hardware --------------------------------
// LED integrado del XIAO ESP32-S3: GPIO21, activo en BAJO.
static const uint8_t  PIN_LED           = 21;
// I2C por defecto del XIAO: SDA = GPIO5 (D4), SCL = GPIO6 (D5).
static const uint32_t I2C_CLOCK_HZ      = 400000;

// ----- Constantes de temporización ---------------------------
static const uint32_t INTERVALO_LED_MS  = 500;
static const uint32_t INTERVALO_LOG_MS  = 1000;
static const uint32_t FRAME_MS          = 33;   // ~30 fps

// ----- Versión de firmware -----------------------------------
static const char* FW_VERSION = "0.2.0-etapa1";

// ----- Display -----------------------------------------------
// SSD1309 128x64 I2C (serigrafía: OLED M154_4P). Si la imagen
// sale corrida, probar la variante NONAME2.
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

// ----- Estado ------------------------------------------------
static uint32_t ultimoLed    = 0;
static uint32_t ultimoLog    = 0;
static uint32_t ultimoFrame  = 0;
static bool     ledEncendido = false;
static uint32_t framesEnVentana = 0;
static uint32_t fpsActual    = 0;

// Pelota de prueba (rebota por la pantalla)
static float bx = 64, by = 32, bvx = 2.1f, bvy = 1.4f;

// ------------------------------------------------------------
// Scan del bus I2C: imprime cada dirección que responde.
static void scanI2C() {
    Serial.println("[i2c] escaneando bus...");
    uint8_t encontrados = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[i2c] dispositivo en 0x%02X\n", addr);
            encontrados++;
        }
    }
    if (encontrados == 0) {
        Serial.println("[i2c] ATENCION: no se encontro ningun dispositivo");
        Serial.println("[i2c] revisar cableado: SDA->D4, SCL->D5, VCC->3V3, GND->GND");
    }
}

// ------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    delay(1500);  // dar tiempo al USB CDC a enumerar

    Serial.println("=========================================");
    Serial.printf ("  espToy boot OK — Etapa 1  |  FW: %s\n", FW_VERSION);
    Serial.println("=========================================");

    pinMode(PIN_LED, OUTPUT);
    digitalWrite(PIN_LED, HIGH);  // apagado (activo en BAJO)

    Wire.begin();                 // SDA=GPIO5, SCL=GPIO6 (default XIAO)
    Wire.setClock(I2C_CLOCK_HZ);
    scanI2C();

    if (!u8g2.begin()) {
        Serial.println("[oled] ERROR: u8g2.begin() fallo");
    } else {
        Serial.println("[oled] inicializado OK (SSD1309 NONAME0, HW I2C)");
    }
    u8g2.setBusClock(I2C_CLOCK_HZ);
}

// ------------------------------------------------------------
void loop() {
    uint32_t ahora = millis();

    // --- LED de vida -----------------------------------------
    if (ahora - ultimoLed >= INTERVALO_LED_MS) {
        ultimoLed = ahora;
        ledEncendido = !ledEncendido;
        digitalWrite(PIN_LED, ledEncendido ? LOW : HIGH);
    }

    // --- Frame de animación a ~30 fps ------------------------
    if (ahora - ultimoFrame >= FRAME_MS) {
        ultimoFrame = ahora;
        framesEnVentana++;

        // Física de la pelota de prueba
        bx += bvx;
        by += bvy;
        if (bx < 4 || bx > 123) bvx = -bvx;
        if (by < 16 || by > 59) bvy = -bvy;

        u8g2.clearBuffer();
        u8g2.setFont(u8g2_font_7x14B_tf);
        u8g2.drawStr(24, 12, "espToy vive!");
        u8g2.drawHLine(0, 14, 128);
        u8g2.drawDisc((int)bx, (int)by, 4);
        u8g2.setFont(u8g2_font_5x7_tf);
        char fpsStr[16];
        snprintf(fpsStr, sizeof(fpsStr), "%lu fps", (unsigned long)fpsActual);
        u8g2.drawStr(2, 62, fpsStr);
        u8g2.sendBuffer();
    }

    // --- Log de estado 1/s -----------------------------------
    if (ahora - ultimoLog >= INTERVALO_LOG_MS) {
        ultimoLog = ahora;
        fpsActual = framesEnVentana;
        framesEnVentana = 0;
        Serial.printf("espToy | fps: %lu | uptime: %lus | heap libre: %lu\n",
                      (unsigned long)fpsActual,
                      (unsigned long)(ahora / 1000),
                      (unsigned long)ESP.getFreeHeap());
    }
}
