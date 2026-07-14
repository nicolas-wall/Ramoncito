// =============================================================
//  espToy — config.h
//  Único lugar para pines, tiempos y umbrales. Ningún .cpp
//  debe tener números mágicos propios.
// =============================================================
#pragma once
#include <Arduino.h>

// ----- Versión ----------------------------------------------
#define FW_VERSION "0.8.0-interaccion"

// ----- Pines (XIAO ESP32-S3: Dx -> GPIO real) ---------------
static const uint8_t PIN_LED       = 21;  // LED integrado, activo en BAJO
static const uint8_t PIN_BTN_A     = 1;   // D0 — botón oculto (a GND, pull-up interno)
static const uint8_t PIN_TOUCH     = 3;   // D2 — sensor táctil (caricia en la cabeza)
static const uint8_t PIN_TOUCH_PIE = 7;   // D8 — sensor táctil del pie (cosquillas)
static const uint8_t PIN_BUZZER    = 4;   // D3 — buzzer pasivo (PWM LEDC)
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
static const uint32_t TOUCH_LOCKOUT_BOTON_MS = 800;  // ignorar touch tras apretar el botón (toques fantasma por acople)

// ----- Reacciones (duraciones en ms) --------------------------
static const uint32_t REACCION_BTN_MS    = 1500;
static const uint32_t REACCION_TOUCH_MS  = 2000;
static const uint32_t REACCION_TICKLE_MS = 1200;

// ----- Cosquillas (pie) ---------------------------------------
// Después de TICKLE_SEGUIDAS_MAX cosquillas dentro de TICKLE_VENTANA_MS → enojo
static const uint8_t  TICKLE_SEGUIDAS_MAX = 3;
static const uint32_t TICKLE_VENTANA_MS   = 6000;
// Cooldown de malhumor tras enojarse por cosquillas de más (§1.2)
static const uint32_t MALHUMOR_MS         = 60000;  // 60 s base (×2 gruñón, ÷2 alegre — Etapa B)

// ----- Expresiones aleatorias durante idle --------------------
static const uint32_t GUINO_RAND_MIN_MS    = 15000;   //  15 s entre guiños
static const uint32_t GUINO_RAND_MAX_MS    = 45000;   //  45 s
static const uint32_t SOSP_RAND_MIN_MS     = 120000;  //   2 min entre sospechas
static const uint32_t SOSP_RAND_MAX_MS     = 360000;  //   6 min
static const uint32_t RAND_EXPR_DUR_MS     = 1500;    // duración normal
static const uint32_t QUEHACER_EXPR_DUR_MS = 4000;    // duración en modo "qué hacemos"

// ----- Interacciones nocturnas (§1.3) --------------------------
static const uint32_t CARICIA_NOCHE_VENTANA_MS  = 30000;  // 30 s: ventana para la 2.ª caricia
static const uint32_t REACCION_NOCHE_FELIZ_MS   = 2000;   // duración cara FELIZ nocturna
static const uint32_t REACCION_NOCHE_ENOJO_MS   = 2500;   // duración cara ENOJADO nocturna

// ----- Inactividad y standby (pantalla apagada) ---------------
// Cada 10 min sin interacción → cara SOSPECHOSO ("¿qué pasa?"). Con el
// standby a los 30 min, aparece a los 10 y 20 min antes del apagado.
static const uint32_t INACTIVIDAD_QUEHACER_MS = 10UL * 60UL * 1000UL;
static const uint32_t INACTIVIDAD_STANDBY_MS  = 30UL * 60UL * 1000UL; // 30 min despierto → pantalla off
static const uint32_t DORMIDO_STANDBY_MS      = 15UL * 60UL * 1000UL; // 15 min durmiendo → off

// ----- Animaciones idle (motor de ojos, doc 03) ---------------
static const uint32_t PARPADEO_MIN_MS = 2000;
static const uint32_t PARPADEO_MAX_MS = 6000;
static const uint32_t MIRADA_MIN_MS   = 3000;
static const uint32_t MIRADA_MAX_MS   = 8000;
static const float    MIRADA_RANGO_PX = 5.0f;
static const float    EASING_EXPRESION = 0.25f;
static const float    EASING_MIRADA    = 0.12f;

// ----- Motor de animaciones por expresión (doc 06 §2) ----------
static const uint32_t ANIM_INTRO_MS  = 400;   // fase de entrada (pop con overshoot)
static const uint32_t ANIM_OUTRO_MS  = 250;   // fase de salida (squash antes de cambiar)
static const float    ANIM_OVERSHOOT          = 0.18f; // sobrepico de escala en INTRO
static const float    ANIM_OVERSHOOT_SORPRESA = 0.35f; // SORPRENDIDO: pop más marcado
static const float    ANIM_SQUASH_MIN         = 0.30f; // altura mínima al final del OUTRO
// Loop por expresión:
static const float    ANIM_FELIZ_AMPL_PX   = 2.0f;   // rebote vertical FELIZ
static const float    ANIM_FELIZ_VEL       = 0.28f;  // rad/frame del rebote
static const float    ANIM_RISA_AMPL_PX   = 3.0f;   // rebote vertical RISA (más enérgico)
static const float    ANIM_RISA_VEL       = 0.40f;  // rad/frame del rebote RISA
static const float    ANIM_AMOR_PULSO      = 0.10f;  // ±10 % de tamaño en AMOR
static const float    ANIM_AMOR_VEL        = 0.20f;  // rad/frame del pulso
static const float    ANIM_DORMIDO_AMPL_PX = 2.0f;   // respiración amplia dormido
static const float    ANIM_DORMIDO_VEL     = 0.042f; // respiración lenta (~0.2 Hz)
static const uint32_t ANIM_ENOJADO_TEMBLOR_MS = 80;  // periodo del temblor horizontal
static const uint32_t ANIM_ABURRIDO_CICLO_MS  = 5000; // párpado baja y sube
static const float    ANIM_SOSP_RANGO_PX   = 4.0f;   // barrido de mirada SOSPECHOSO
static const float    ANIM_SOSP_VEL        = 0.0015f; // rad/ms del barrido
// Partículas (corazones, Zzz, lágrima):
static const uint8_t  ANIM_PARTICULAS_MAX    = 4;
static const uint32_t ANIM_CORAZON_SPAWN_MS  = 700;   // AMOR: cadencia de corazones
static const uint16_t ANIM_CORAZON_VIDA_MS   = 1600;
static const uint32_t ANIM_ZZZ_SPAWN_MS      = 900;   // DORMIDO: cadencia de Z
static const uint16_t ANIM_ZZZ_VIDA_MS       = 2400;
static const uint32_t ANIM_LAGRIMA_SPAWN_MS  = 4000;  // TRISTE: una lágrima cada 4 s
static const uint16_t ANIM_LAGRIMA_VIDA_MS   = 2000;

// ----- Gestos idle (doc 03 §3.4) --------------------------------
// Intervalos entre disparos (aleatorios dentro del rango)
static const uint32_t GESTO_BOSTEZO_MIN_MS    = 5UL  * 60UL * 1000UL;   //  5 min
static const uint32_t GESTO_BOSTEZO_MAX_MS    = 10UL * 60UL * 1000UL;   // 10 min
static const uint32_t GESTO_SACUDIDA_MIN_MS   = 8UL  * 60UL * 1000UL;   //  8 min
static const uint32_t GESTO_SACUDIDA_MAX_MS   = 15UL * 60UL * 1000UL;   // 15 min
static const uint32_t GESTO_MIRADA_MIN_MS     = 3UL  * 60UL * 1000UL;   //  3 min
static const uint32_t GESTO_MIRADA_MAX_MS     = 7UL  * 60UL * 1000UL;   //  7 min

// Duraciones internas del bostezo (tramos acumulados)
static const uint32_t BOSTEZO_T_AGRANDA_MS    = 400;   // 0–400  ms: ojos se agrandan
static const uint32_t BOSTEZO_T_CIERRA_MS     = 1200;  // 400–1200 ms: se cierran
static const uint32_t BOSTEZO_T_CERRADO_MS    = 1500;  // 1200–1500 ms: quietos cerrados
static const uint32_t BOSTEZO_T_TOTAL_MS      = 1900;  // 1500–1900 ms: reabre

// Escala máxima del bostezo (los ojos se agrandan a ×1.25)
static const float    BOSTEZO_ESCALA_MAX      = 1.25f;

// Duración y amplitud de la sacudida de cabeza
static const uint32_t SACUDIDA_DURACION_MS    = 260;
static const float    SACUDIDA_AMPLITUD_PX    = 3.0f;

// Duración de la mirada fija (párpados caídos, sin parpadeo)
static const uint32_t MIRADA_FIJA_DURACION_MS = 4000;
// Fracción de _leftTgt.h para el párpado levemente caído
static const float    MIRADA_FIJA_LID_FRAC    = 0.15f;

// ----- Sonido (buzzer pasivo por LEDC) -------------------------
static const uint8_t  BUZZER_LEDC_CANAL = 0;
static const uint8_t  BUZZER_LEDC_RES   = 8;     // bits de resolución
static const bool     SONIDO_HABILITADO_DEFAULT = true; // sin buzzer no molesta

// ----- Cerebro Tamagotchi (mood, doc 02/03) --------------------
// Decaimiento POR TICK de 5 minutos (escalado por TIME_SCALE).
// (El tick de 1 min original agotaba la energía en <1 h — demasiado
// agresivo para un juguete de escritorio; verificado en uso real.)
static const uint8_t  MOOD_DECAY_ENERGIA_PM   = 1;  // energía -1/tick (≈ -12/hora; con 2 el ciclo siesta era demasiado frecuente)
static const uint8_t  MOOD_DECAY_FELICIDAD_PM = 3;  // felicidad -3/tick (≈ -36/hora; ~50 min de neutral a triste)
static const uint8_t  MOOD_SUBE_ABURRIM_PM    = 5;  // aburrimiento +5/tick (≈ +60/hora; ~75 min de 0 a aburrido)
static const uint8_t  MOOD_RECUPERA_ENERGIA_PT = 25; // energía +25/tick mientras duerme — recarga rápida para no ciclar
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
// false = no intentar conectarse a ninguna red; solo levantar el AP de setup
//         (espToy-setup) para dar la hora desde el teléfono. Ideal si el router
//         es errático o de 5 GHz. true = intentar conectar a las credenciales
//         guardadas y sincronizar por NTP.
static const bool     WIFI_INTENTAR_STA = false;

// 22 s: en modo STA puro (primer intento), asociar + DHCP en un router
// 2.4 GHz congestionado puede tardar 15-20 s. Con 10 s se vencía y caía
// al portal AP_STA (donde el AP se vuelve inestable). Darle tiempo al
// primer intento limpio es lo que más mejora la conexión.
static const uint32_t WIFI_TIMEOUT_MS   = 22000;
static const char*    NTP_SERVER        = "pool.ntp.org";
static const uint32_t NTP_RESYNC_MS     = 24UL * 3600UL * 1000UL; // 1 vez por día
static const char*    PORTAL_AP_SSID    = "espToy-setup";
// El portal se levanta si no hay credenciales o falla la conexión.

// ----- OTA por web (subir firmware desde el navegador) ---------
// Para actualizar desde el teléfono:
//   1. Conectarse al AP espToy-setup
//   2. Abrir http://192.168.4.1/update
//   3. Usuario: esptoy  /  Clave: esptoy
//   4. Elegir el archivo firmware.bin y pulsar "Actualizar"
static const bool  OTA_HABILITADO = true;
static const char* OTA_USUARIO    = "esptoy";
static const char* OTA_CLAVE      = "esptoy";

// ----- Personalidad (doc 06 §3) --------------------------------
// Valores iniciales y umbrales de rasgo
static const uint8_t  PERSONALIDAD_INI            = 50;
static const uint8_t  PERSONALIDAD_UMBRAL_ALTO    = 65;   // rasgo > 65 → efecto "alto"
static const uint8_t  PERSONALIDAD_UMBRAL_BAJO    = 35;   // rasgo < 35 → texto "bajo" (Etapa C)

// Plasticidad: primeros N días el factor es ×1.0; después ×FACTOR_MADURO
static const uint8_t  PERSONALIDAD_DIAS_FORMACION = 7;
static const float    PERSONALIDAD_FACTOR_MADURO  = 0.25f;

// Guardado diferido en NVS
static const uint32_t PERSONALIDAD_GUARDAR_CADA_MS = 5UL * 60UL * 1000UL;

// Límite de cosquillas cuando el gruñón es alto (en vez de TICKLE_SEGUIDAS_MAX)
static const uint8_t  TICKLE_SEGUIDAS_GRUNON      = 2;

// Decay de felicidad si alegre alto: 1/tick (en vez de MOOD_DECAY_FELICIDAD_PM)
static const uint8_t  PERSONALIDAD_DECAY_FELIZ_ALEGRE = 1;

// Recuperación de energía: delta base ±10 si energetico/perezoso alto
static const uint8_t  PERSONALIDAD_RECUPERA_DELTA     = 10;

// Umbrales de siesta según perezoso
static const uint8_t  PERSONALIDAD_UMBRAL_SIESTA_BASE    = 5;
static const uint8_t  PERSONALIDAD_UMBRAL_SIESTA_PEREZOSO = 10;

// Umbrales de dominantExpression modulados por personalidad (no números mágicos)
// Cara ENOJADO: felicidad < ENOJO_FEL y aburrimiento > ENOJO_ABUR (base; gruñón alto más fácil)
static const uint8_t  PERSONALIDAD_ENOJO_FEL_GRUNON  = 25;  // gruñón alto: fel < 25
static const uint8_t  PERSONALIDAD_ENOJO_ABUR_GRUNON = 50;  // gruñón alto: abur > 50
static const uint8_t  PERSONALIDAD_ENOJO_FEL_BASE    = 20;  // base normal: fel < 20
static const uint8_t  PERSONALIDAD_ENOJO_ABUR_BASE   = 60;  // base normal: abur > 60
// Cara FELIZ: felicidad > FELIZ_FEL y aburrimiento < FELIZ_ABUR (alegre alto más fácil)
static const uint8_t  PERSONALIDAD_FELIZ_FEL_ALEGRE  = 60;  // alegre alto: fel > 60
static const uint8_t  PERSONALIDAD_FELIZ_ABUR_BASE   = 40;  // compartido: abur < 40
static const uint8_t  PERSONALIDAD_FELIZ_FEL_BASE    = 70;  // base normal: fel > 70
