// =============================================================
//  espToy — config.h
//  Único lugar para pines, tiempos y umbrales. Ningún .cpp
//  debe tener números mágicos propios.
// =============================================================
#pragma once
#include <Arduino.h>

// ----- Versión ----------------------------------------------
#define FW_VERSION "0.3.0-etapa3"

// ----- Pines (XIAO ESP32-S3: Dx -> GPIO real) ---------------
static const uint8_t PIN_LED     = 21;  // LED integrado, activo en BAJO
static const uint8_t PIN_BTN_A   = 1;   // D0 — botón A (a GND, pull-up interno)
static const uint8_t PIN_BTN_B   = 2;   // D1 — botón B (a GND, pull-up interno)
static const uint8_t PIN_TOUCH   = 3;   // D2 — sensor táctil capacitivo
static const uint8_t PIN_BUZZER  = 4;   // D3 — buzzer pasivo (PWM LEDC), aún sin usar
// I2C del OLED: SDA=GPIO5 (D4), SCL=GPIO6 (D5) — defaults de Wire en el XIAO

// ----- Display -----------------------------------------------
static const uint32_t I2C_CLOCK_HZ = 400000;

// ----- Timing global -----------------------------------------
static const uint32_t FRAME_MS         = 33;    // ~30 fps
static const uint32_t INTERVALO_LOG_MS = 1000;  // log de estado por serial

// ----- Botones ------------------------------------------------
static const uint32_t DEBOUNCE_MS = 30;

// ----- Touch capacitivo (ESP32-S3) ----------------------------
// OJO: en el ESP32-S3 touchRead() DEVUELVE MÁS al tocar (al revés
// que el ESP32 original). Umbral relativo sobre la línea base.
static const uint16_t TOUCH_MUESTRAS_CALIB = 50;    // lecturas para baseline al boot
static const float    TOUCH_FACTOR_UMBRAL  = 1.15f; // toque si valor > baseline * factor
static const uint8_t  TOUCH_LECTURAS_CONFIRMA = 3;  // lecturas consecutivas para confirmar
static const uint32_t TOUCH_POLL_MS        = 20;    // período de muestreo del touch

// ----- Reacciones (duraciones en ms) --------------------------
static const uint32_t REACCION_BTN_MS   = 1500;
static const uint32_t REACCION_TOUCH_MS = 2000;

// ----- Animaciones idle (motor de ojos, doc 03) ---------------
static const uint32_t PARPADEO_MIN_MS = 2000;
static const uint32_t PARPADEO_MAX_MS = 6000;
static const uint32_t MIRADA_MIN_MS   = 3000;
static const uint32_t MIRADA_MAX_MS   = 8000;
static const float    MIRADA_RANGO_PX = 5.0f;   // offset máximo de mirada errante
static const float    EASING_EXPRESION = 0.25f; // factor de interpolación por frame
static const float    EASING_MIRADA    = 0.12f;
