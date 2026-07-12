// =============================================================
//  espToy — config.h
//  Único lugar para pines, tiempos y umbrales. Ningún .cpp
//  debe tener números mágicos propios.
// =============================================================
#pragma once
#include <Arduino.h>

// ----- Versión ----------------------------------------------
#define FW_VERSION "0.6.0-etapa6"

// ----- Pines (XIAO ESP32-S3: Dx -> GPIO real) ---------------
static const uint8_t PIN_LED     = 21;  // LED integrado, activo en BAJO
static const uint8_t PIN_BTN_A   = 1;   // D0 — botón A (a GND, pull-up interno)
static const uint8_t PIN_BTN_B   = 2;   // D1 — botón B (a GND, pull-up interno)
static const uint8_t PIN_TOUCH   = 3;   // D2 — sensor táctil capacitivo
static const uint8_t PIN_BUZZER  = 4;   // D3 — buzzer pasivo (PWM LEDC)
// I2C del OLED: SDA=GPIO5 (D4), SCL=GPIO6 (D5) — defaults de Wire en el XIAO

// ----- Display -----------------------------------------------
static const uint32_t I2C_CLOCK_HZ = 400000;

// ----- Timing global -----------------------------------------
static const uint32_t FRAME_MS         = 33;    // ~30 fps
static const uint32_t INTERVALO_LOG_MS = 1000;  // log de estado por serial

// Escala de tiempo para pruebas: 1 = producción. Con 60, un
// "minuto" de decaimiento pasa en 1 segundo real.
static const uint32_t TIME_SCALE = 1;

// ----- Botones ------------------------------------------------
static const uint32_t DEBOUNCE_MS = 30;

// ----- Touch capacitivo (ESP32-S3: el valor SUBE al tocar) ----
static const uint16_t TOUCH_MUESTRAS_CALIB    = 50;
static const float    TOUCH_FACTOR_UMBRAL     = 1.15f;
static const uint8_t  TOUCH_LECTURAS_CONFIRMA = 3;
static const uint32_t TOUCH_POLL_MS           = 20;

// ----- Reacciones (duraciones en ms) --------------------------
static const uint32_t REACCION_BTN_MS   = 1500;
static const uint32_t REACCION_TOUCH_MS = 2000;
static const uint32_t REACCION_ENOJO_NOCHE_MS = 2500; // protesta si lo despiertan

// ----- Animaciones idle (motor de ojos, doc 03) ---------------
static const uint32_t PARPADEO_MIN_MS = 2000;
static const uint32_t PARPADEO_MAX_MS = 6000;
static const uint32_t MIRADA_MIN_MS   = 3000;
static const uint32_t MIRADA_MAX_MS   = 8000;
static const float    MIRADA_RANGO_PX = 5.0f;
static const float    EASING_EXPRESION = 0.25f;
static const float    EASING_MIRADA    = 0.12f;

// ----- Sonido (buzzer pasivo por LEDC) -------------------------
static const uint8_t  BUZZER_LEDC_CANAL = 0;
static const uint8_t  BUZZER_LEDC_RES   = 8;     // bits de resolución
static const bool     SONIDO_HABILITADO_DEFAULT = true; // sin buzzer no molesta

// ----- Cerebro Tamagotchi (mood, doc 02/03) --------------------
// Decaimiento POR TICK de 5 minutos (escalado por TIME_SCALE).
// (El tick de 1 min original agotaba la energía en <1 h — demasiado
// agresivo para un juguete de escritorio; verificado en uso real.)
static const uint8_t  MOOD_DECAY_ENERGIA_PM   = 2;  // energía -2/tick (≈ -24/hora)
static const uint8_t  MOOD_DECAY_FELICIDAD_PM = 1;  // felicidad -1/tick
static const uint8_t  MOOD_SUBE_ABURRIM_PM    = 2;  // aburrimiento +2/tick
static const uint8_t  MOOD_RECUPERA_ENERGIA_PT = 3; // energía +3/tick mientras duerme/descansa
static const uint32_t MOOD_TICK_MS            = 5UL * 60UL * 1000UL; // 5 min (dividido por TIME_SCALE)
// Valores iniciales si no hay nada guardado:
static const uint8_t  MOOD_INI_FELICIDAD = 50;
static const uint8_t  MOOD_INI_ENERGIA   = 80;
static const uint8_t  MOOD_INI_ABURRIM   = 0;
// Persistencia NVS:
static const uint32_t MOOD_GUARDAR_CADA_MS = 5UL * 60UL * 1000UL; // máx. cada 5 min
// Decaimiento offline: tope de tiempo apagado que se aplica
static const uint32_t OFFLINE_DECAY_TOPE_S = 48UL * 3600UL;       // 48 h

// ----- Ciclo día/noche -----------------------------------------
static const int  HORA_DORMIR    = 22;  // desde las 22:00...
static const int  HORA_DESPERTAR = 7;   // ...hasta las 07:00
static const long TZ_OFFSET_S    = -3L * 3600L;  // Argentina (UTC-3, sin DST)

// ----- WiFi / NTP / Portal --------------------------------------
static const uint32_t WIFI_TIMEOUT_MS   = 10000;
static const char*    NTP_SERVER        = "pool.ntp.org";
static const uint32_t NTP_RESYNC_MS     = 24UL * 3600UL * 1000UL; // 1 vez por día
static const char*    PORTAL_AP_SSID    = "espToy-setup";
// El portal se levanta si no hay credenciales o falla la conexión.
