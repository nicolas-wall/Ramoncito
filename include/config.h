// =============================================================
//  Ramoncito — config.h
//  Único lugar para pines, tiempos y umbrales. Ningún .cpp
//  debe tener números mágicos propios.
// =============================================================
#pragma once
#include <Arduino.h>

// ----- Versión ----------------------------------------------
// Semver puro: el parser de OTA compara major.minor.patch sin sufijos
#define FW_VERSION "0.9.11"

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

// ----- Ronquido al dormir -------------------------------------
// El ronquido suena periódicamente solo durante los primeros
// RONQUIDO_VENTANA_MS tras quedarse dormido; después, silencio
// (no se repite para siempre).
static const uint32_t RONQUIDO_VENTANA_MS = 10000;  // 10 s de ronquidos al inicio del sueño

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

// ----- Expresión ILUSIONADO (ojos brillantes al ser alzado) ------
// Velocidad del pulso de brillo en el loop (rad/frame).
// Con FRAME_MS=33 ms y vel=0.15 rad/frame → ~1 ciclo cada 1.4 s.
static const float    ANIM_ILUSIONADO_VEL        = 0.15f;  // rad/frame del twinkle
// Amplitud del pulso de escala (porcentaje): ±6% de tamaño para el "latido".
static const float    ANIM_ILUSIONADO_PULSO       = 0.06f;  // ±6 % de escala
// Desplazamiento vertical del highlight dentro del ojo (arriba del centro).
// Valor positivo en U8g2 = hacia abajo; usamos negativo = hacia arriba.
static const float    ANIM_ILUSIONADO_HIGHLIGHT_OY = -4.5f; // px arriba del centro
// Desplazamiento horizontal del highlight (hacia la esquina interior).
static const float    ANIM_ILUSIONADO_HIGHLIGHT_OX = -3.5f; // px hacia la izquierda
// Radio base del highlight en reposo (disco relleno pequeño).
static const float    ANIM_ILUSIONADO_HIGHLIGHT_R  = 2.5f;  // px

// ----- Expresión MAREADO (ojos en espiral giratoria) -----------
// Número de vueltas de la espiral en cada ojo (controla cuán densa es).
static const float    ANIM_MAREADO_VUELTAS     = 2.5f;   // vueltas de la espiral
// Paso angular entre puntos consecutivos de la espiral (rad).
// Valores chicos = espiral más suave pero más operaciones; ~0.25 rad es buen balance.
static const float    ANIM_MAREADO_PASO_RAD    = 0.25f;  // rad entre puntos
// Factor de escala del radio por unidad de ángulo (qué tan abierta es la espiral).
// A mayor valor, la espiral ocupa más del área del ojo.
static const float    ANIM_MAREADO_K           = 1.4f;   // px/rad (radio = k * t)
// Velocidad de rotación de la espiral en el loop (rad/frame).
// Con FRAME_MS=33 ms y vel=0.08 rad/frame → ~1 vuelta cada 2.6 s.
static const float    ANIM_MAREADO_VEL_ROT     = 0.08f;  // rad/frame de giro del remolino

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
// Volumen del buzzer pasivo: porcentaje del duty máximo (2^8-1 = 255).
// Al 50% ledcWriteTone fuerza el máximo; bajando el duty se reduce el volumen.
// 20% ≈ duty=51 → audible pero no agresivo. Rango recomendado: 10-35.
static const uint8_t  SOUND_VOLUMEN_PCT = 10;    // porcentaje de duty máximo (0-100)

// ----- Menú — long-press del botón en página 3 -----------------
// Tiempo mínimo mantenido para disparar la acción de long-press (toggle sonido).
static const uint32_t MENU_LONGPRESS_MS = 800;   // ms presionado para reconocer long-press

// Timeout de confirmación del renacer (página 2): si no llega el
// segundo toque (pie) dentro de este tiempo, se cancela la operación.
static const uint32_t MENU_RENACER_CONFIRM_MS = 6000;  // 6 s para confirmar

// ----- Animación de nacimiento — encendido CRT -----------------
// Duración total ~2.1 s dividida en 4 fases:
//
//   FASE 0 — punto→línea (~260 ms):
//     Pantalla negra; una línea horizontal blanca de grosor 2 px
//     crece desde el centro (px 64) hacia ambos lados hasta ocupar
//     todo el ancho (128 px). Efecto "tubo calentándose".
//
//   FASE 1 — apertura vertical (~560 ms):
//     La línea central se abre en vertical: rectángulo relleno blanco
//     centrado en y=32, altura crece de 2 px hasta 64 px (pantalla
//     llena → flash blanco). Sensación de "el tubo abre la imagen".
//
//   FASE 2 — estática CRT (~480 ms):
//     Fondo negro con 150–250 píxeles blancos en posiciones aleatorias
//     (ruido de sintonía). Algunas líneas horizontales de una sola
//     fila refuerzan el look de scanlines retro.
//
//   FASE 3 — revelado de la cara (~820 ms):
//     La carita aparece de arriba hacia abajo: se dibuja con
//     face.render() y se tapa la parte inferior con un rectángulo
//     negro cuya altura baja con el progreso (barrido de scanline).
//     Al llegar al 100% la cara queda completa.
//
//   Al terminar → IDLE con FELIZ (igual que antes).
static const uint32_t ANIM_NACIMIENTO_F0_MS = 260;   // ms línea horizontal crece
static const uint32_t ANIM_NACIMIENTO_F1_MS = 560;   // ms apertura vertical (flash)
static const uint32_t ANIM_NACIMIENTO_F2_MS = 480;   // ms estática/ruido CRT
static const uint32_t ANIM_NACIMIENTO_F3_MS = 820;   // ms revelado de la cara
static const uint32_t ANIM_NACIMIENTO_TOTAL_MS =
    ANIM_NACIMIENTO_F0_MS + ANIM_NACIMIENTO_F1_MS +
    ANIM_NACIMIENTO_F2_MS + ANIM_NACIMIENTO_F3_MS;
// Cantidad de píxeles aleatorios en la fase de estática
static const uint16_t ANIM_NACIMIENTO_RUIDO_PX  = 200;  // píxeles blancos por frame
// Altura mínima de la línea inicial en la fase 1 (hereda de la fase 0)
static const uint8_t  ANIM_NACIMIENTO_LINEA_GROSOR = 2;  // px de grosor de la línea CRT

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
// true = intentar conectar a las credenciales guardadas y sincronizar por NTP.
//        Se reactiva STA porque el auto-OTA desde GitHub Releases necesita
//        internet; el portal captivo sigue disponible como fallback si la
//        conexión falla o no hay credenciales configuradas.
static const bool     WIFI_INTENTAR_STA = true;

// 22 s: en modo STA puro (primer intento), asociar + DHCP en un router
// 2.4 GHz congestionado puede tardar 15-20 s. Con 10 s se vencía y caía
// al portal AP_STA (donde el AP se vuelve inestable). Darle tiempo al
// primer intento limpio es lo que más mejora la conexión.
static const uint32_t WIFI_TIMEOUT_MS   = 22000;
static const char*    NTP_SERVER        = "pool.ntp.org";
static const uint32_t NTP_RESYNC_MS     = 24UL * 3600UL * 1000UL; // 1 vez por día
static const char*    PORTAL_AP_SSID    = "Ramoncito-setup";
// El portal se levanta si no hay credenciales o falla la conexión.

// ----- OTA por web (subir firmware desde el navegador) ---------
// Para actualizar desde el teléfono:
//   1. Conectarse al AP Ramoncito-setup
//   2. Abrir http://192.168.4.1/update
//   3. Usuario: ramoncito  /  Clave: ramoncito
//   4. Elegir el archivo firmware.bin y pulsar "Actualizar"
static const bool  OTA_HABILITADO = true;
static const char* OTA_USUARIO    = "ramoncito";
static const char* OTA_CLAVE      = "ramoncito";

// ----- Auto-OTA desde GitHub Releases (doc: actualización de flota) -----
// El dispositivo verifica periódicamente si hay una versión nueva en GitHub
// y la descarga/instala de forma autónoma cuando está conectado a internet.
static const bool     OTA_AUTO_HABILITADO   = true;
static const char*    OTA_VERSION_URL  = "https://github.com/nicolas-wall/Ramoncito/releases/latest/download/version.json";
static const char*    OTA_FIRMWARE_URL = "https://github.com/nicolas-wall/Ramoncito/releases/latest/download/firmware.bin";
static const uint32_t OTA_CHECK_BOOT_MS      = 90UL * 1000UL;           // primer chequeo 90 s después del boot
static const uint32_t OTA_CHECK_INTERVALO_MS = 24UL * 3600UL * 1000UL; // después, 1 vez por día

// ----- Panel web en la LAN (menú por teléfono en la misma red) ---------
// Con esto el toy mantiene un servidor web escuchando sobre la STA (tu WiFi
// de casa), así el teléfono —conectado a la MISMA red, sin cambiar de red—
// entra a http://ramoncito.local (o a la IP del toy) y ve un dashboard con las
// stats, la personalidad y botones (sonido, chequear/instalar OTA, renacer,
// cambiar WiFi). Protegido con OTA_USUARIO/OTA_CLAVE (Basic Auth).
// Nota: mantener la STA + el server siempre vivos consume algo más y mete
// algo de ruido en el táctil; ideal enchufado. Requiere WIFI_INTENTAR_STA.
static const bool     PANEL_LAN_HABILITADO = true;
static const char*    PANEL_MDNS_HOST      = "ramoncito";   // → http://ramoncito.local

// ==== IMU (MPU6050 en GY-521) ====
// Dirección I2C: AD0 a 3V3 → 0x69 (0x68 lo ocupa el RTC DS3231)
static const uint8_t  IMU_ADDR = 0x69;

// ── Sacudida ──────────────────────────────────────────────────
// Umbral de magnitud de aceleración (en g) para detectar un golpe/sacudida.
// En reposo el chip mide ~1 g (gravedad). Un sacudón típico da 2.5–4 g.
static const float    IMU_SACUDIDA_UMBRAL      = 1.8f;   // g: por encima → sacudida

// Debounce: tiempo mínimo entre dos picos contados (evita que un solo golpe
// cuente varias veces por rebote mecánico).
static const uint32_t IMU_SACUDIDA_DEBOUNCE_MS = 200;    // ms

// Ventana de tiempo dentro de la cual se acumulan sacudidas consecutivas.
// Si la siguiente sacudida llega antes de que expire esta ventana, se suma al contador.
static const uint32_t IMU_SACUDIDA_VENTANA_MS  = 1500;   // ms

// Umbral de sacudidas acumuladas en la ventana para disparar enojo en vez de sorpresa.
// 1–(MAX-1) sacudidas → SORPRENDIDO; MAX o más → ENOJADO.
static const uint8_t  IMU_SACUDIDA_MAX         = 3;      // golpes: ≥ 3 → enojo

// ── Levantado por orientación sostenida ───────────────────────
// La detección ya no usa banda de magnitud sino comparación de ángulo
// entre la dirección actual de la gravedad filtrada y el baseline de reposo.

// Suavizado EMA rápido para seguir la gravedad frame a frame.
// Alpha chico = más suavizado (sacudidas no desplazan el vector filtrado).
static const float    IMU_GRAV_ALPHA           = 0.05f;  // factor EMA de gravedad filtrada

// Suavizado EMA muy lento para adaptar el baseline de reposo cuando quieto.
// Permite que el baseline se ajuste si el juguete queda en una nueva posición.
static const float    IMU_BASE_ALPHA           = 0.002f; // factor EMA del baseline (≈0.2 % por frame)

// Número de frames iniciales para promediar y establecer el baseline.
// Con FRAME_MS=33 ms: 20 frames ≈ 660 ms de calentamiento.
static const uint8_t  IMU_GRAV_WARMUP_FRAMES   = 20;     // frames de calentamiento al arrancar

// Ángulo mínimo de desvío de orientación para considerar que alzaron el juguete.
// 25° equivale a un giro notable: apoyado en mesa vs. sostenido en mano.
static const float    IMU_LEVANTADO_ANGULO_GRADOS = 25.0f; // grados

// Tiempo mínimo que el ángulo debe superar el umbral de forma sostenida.
// Filtra movimientos breves (sacudida), que no desplazan el EMA más de 0.3-0.5 s.
static const uint32_t IMU_LEVANTADO_SOSTEN_MS  = 400;    // ms sostenido para confirmar

// Debounce del evento "levantado": evita disparos repetidos si lo sostienen.
static const uint32_t IMU_LEVANTADO_DEBOUNCE_MS = 3000;  // ms entre detecciones

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
static const uint8_t  PERSONALIDAD_FELIZ_FEL_ALEGRE  = 85;  // feliz solo con fel > 85
static const uint8_t  PERSONALIDAD_FELIZ_ABUR_BASE   = 40;  // compartido: abur < 40
static const uint8_t  PERSONALIDAD_FELIZ_FEL_BASE    = 85;  // feliz solo con fel > 85
