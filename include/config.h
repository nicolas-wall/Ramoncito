// =============================================================
//  Ramoncito — config.h
//  Único lugar para pines, tiempos y umbrales. Ningún .cpp
//  debe tener números mágicos propios.
// =============================================================
#pragma once
#include <Arduino.h>

// ----- Versión ----------------------------------------------
// Semver puro: el parser de OTA compara major.minor.patch sin sufijos
#define FW_VERSION "0.11.0"

// ----- Pines (XIAO ESP32-S3: Dx -> GPIO real) ---------------
// Pinout ARCADE. Los dos sensores táctiles capacitivos de la etapa
// "mascota" se retiraron: esos pines ahora los usan la palanca y un botón.
//
// Los ejes analógicos deben ir sí o sí a pines del ADC1 (GPIO1..GPIO10):
// el ADC2 queda inutilizable con el WiFi encendido, y acá el WiFi está
// siempre activo para el auto-OTA.
static const uint8_t PIN_LED    = 21;  // LED integrado, activo en BAJO
static const uint8_t PIN_BTN_A  = 1;   // D0  — botón 1 (a GND, pull-up interno) [CABLEADO]
static const uint8_t PIN_BTN_C  = 3;   // D2  — botón 2 / activar (a GND, pull-up) [CABLEADO]
static const uint8_t PIN_BTN_B  = 7;   // D8  — botón 3 / cursor (a GND, pull-up) [CABLEADO]
static const uint8_t PIN_JOY_SW = 9;   // D10 — pulsador de la palanca (a GND, pull-up interno)
static const uint8_t PIN_JOY_X  = 2;   // D1  — eje X analógico (ADC1_CH1)
static const uint8_t PIN_JOY_Y  = 8;   // D9  — eje Y analógico (ADC1_CH7)
//
// Asignación verificada pin por pin con el escáner ('k') y el modo vigilancia
// ('v') el 2026-08-04. GPIO1, GPIO3, GPIO7 y GPIO2 respondieron a un puente
// contra GND; GPIO9 (D10) NO responde ni con jumper directo — su soldadura
// quedó abierta. Por eso GPIO9 se reservó para el pulsador de la palanca, que
// es la función más prescindible del conjunto: duplica al botón C (activar),
// así que si ese pin nunca revive, no se pierde nada.
static const uint8_t PIN_BUZZER = 4;   // D3  — buzzer pasivo (PWM LEDC)
// I2C del OLED: SDA=GPIO5 (D4), SCL=GPIO6 (D5) — defaults de Wire en el XIAO
//
// Nota sobre GPIO3 (D2): es pin de strapping del ESP32-S3 (selección de
// JTAG). Con los eFuses de fábrica se ignora al bootear, y el pull-up
// interno lo deja en HIGH = botón suelto, que es el estado seguro. Por eso
// va un BOTÓN acá y no un eje analógico: un eje en reposo queda a media
// tensión (nivel lógico indefinido durante el boot).

// ----- Display -----------------------------------------------
static const uint32_t I2C_CLOCK_HZ = 400000;

// ----- Timing global -----------------------------------------
static const uint32_t FRAME_MS         = 33;    // ~30 fps
static const uint32_t INTERVALO_LOG_MS = 1000;  // log de estado por serial

// ----- Botones ------------------------------------------------
static const uint32_t DEBOUNCE_MS = 30;

// ----- Palanca (joystick analógico tipo KY-023 / thumbstick PS2) -----
// Mientras no esté el módulo físico, dejar JOYSTICK_PRESENTE en false: los
// ejes NO se leen del ADC (un pin al aire flota y dispararía direcciones
// solo) y se emulan por serial con w/a/s/d — ver procesarComando() en
// main.cpp. Al soldar la palanca, cambiar a true y recompilar.
static const bool     JOYSTICK_PRESENTE      = false;

static const uint16_t JOY_ADC_MAX            = 4095;  // 12 bits de resolución
static const uint16_t JOY_MUESTRAS_CALIB     = 32;    // lecturas para fijar el centro al boot
// Zona muerta: por debajo de esto el eje se reporta como 0. Los thumbsticks
// baratos no vuelven exactamente al centro, sin deadzone hay deriva.
static const float    JOY_DEADZONE           = 0.18f;
// Histéresis para convertir el eje analógico en una dirección discreta:
// activa al superar ON, y no se suelta hasta bajar de OFF. Sin esto, una
// palanca sostenida cerca del umbral genera un tren de eventos.
static const float    JOY_UMBRAL_ON          = 0.50f;
static const float    JOY_UMBRAL_OFF         = 0.35f;
static const uint32_t JOY_POLL_MS            = 15;    // muestreo del ADC (~66 Hz)
// Auto-repeat al mantener la palanca (navegación de menús estilo arcade)
static const uint32_t JOY_REPEAT_DELAY_MS    = 380;   // espera antes del primer repeat
static const uint32_t JOY_REPEAT_MS          = 130;   // cadencia del repeat sostenido
// Si al probar la palanca los ejes van al revés, invertirlos acá (no hay
// que tocar el cableado ni la lógica de los juegos).
static const bool     JOY_INVERTIR_X         = false;
static const bool     JOY_INVERTIR_Y         = true;  // en el KY-023 el ADC de Y crece hacia abajo
// Stub sin hardware: cuánto dura el "golpe" de palanca que simula una tecla
// antes de volver sola al centro.
static const uint32_t JOY_STUB_AUTOCENTRO_MS = 220;

// ----- Arcade -------------------------------------------------
// Duración del deslizamiento entre tarjetas del carrusel. Corto: es
// confirmación de que te moviste, no una animación para mirar. Por encima
// de ~250 ms el menú empieza a sentirse pesado al pasar varias seguidas.
static const uint32_t ARCADE_SLIDE_MS    = 170;
// Sin tocar nada durante este tiempo, el arcade se cierra solo y vuelve a la
// cara. Es largo a propósito: en el menú de sistema 10 s está bien, pero acá
// alguien puede quedarse pensando entre partidas.
static const uint32_t ARCADE_TIMEOUT_MS  = 60000;

// ----- Pong ---------------------------------------------------
// Cancha: la franja de arriba queda para el marcador, el resto es el campo.
static const uint8_t  PONG_TOP_Y         = 12;   // primera fila jugable
static const uint8_t  PONG_BOT_Y         = 63;   // última fila jugable

static const uint8_t  PONG_PALETA_W      = 2;
static const uint8_t  PONG_PALETA_H      = 14;
static const uint8_t  PONG_PALETA_X_JUG  = 3;    // paleta del jugador (izquierda)
static const uint8_t  PONG_PALETA_X_CPU  = 123;  // paleta de la CPU (derecha)
// Velocidad de la paleta del jugador a fondo de palanca (px por frame).
static const float    PONG_PALETA_VEL    = 2.4f;
// La CPU es deliberadamente más lenta que el jugador: es lo único que hace
// que el juego se pueda ganar. Subir esto lo vuelve invencible.
static const float    PONG_CPU_VEL       = 1.5f;
// Tolerancia antes de que la CPU corrija: sin esto tiembla alrededor de la
// pelota y se ve como un tic nervioso.
static const float    PONG_CPU_ZONA_MUERTA = 3.0f;

static const uint8_t  PONG_BOLA_LADO     = 3;    // cuadradito de 3x3
static const float    PONG_BOLA_VEL_INI  = 1.5f; // px/frame al sacar
static const float    PONG_BOLA_VEL_MAX  = 3.4f; // techo tras muchos rebotes
static const float    PONG_BOLA_ACEL     = 0.12f;// se acelera en cada paletazo
// Efecto: cuánto desvía el rebote según dónde pegue en la paleta. 0 = rebote
// plano y aburrido; alto = imposible de controlar.
static const float    PONG_BOLA_EFECTO   = 1.1f;

static const uint8_t  PONG_PUNTOS_GANAR  = 5;
static const uint32_t PONG_SAQUE_MS      = 900;  // pausa antes de cada saque

// ----- Snake --------------------------------------------------
// Grilla de 30x12 celdas de 4 px: 120x48 px, con la barra de puntaje
// arriba y un marco alrededor de la cancha.
static const uint8_t  SNAKE_CELDA      = 4;
static const uint8_t  SNAKE_COLS       = 30;
static const uint8_t  SNAKE_FILAS      = 12;
static const uint8_t  SNAKE_X0         = 4;
static const uint8_t  SNAKE_Y0         = 12;
// Cadencia inicial y cuánto se acorta por cada comida. Toda la dificultad
// del juego sale de acá: la víbora acelera a medida que crece.
static const uint32_t SNAKE_PASO_MS    = 180;
static const uint32_t SNAKE_ACEL_MS    = 4;
static const uint32_t SNAKE_PASO_MIN_MS = 70;

// ----- Breakout -----------------------------------------------
static const uint8_t  BRK_COLS         = 8;
static const uint8_t  BRK_FILAS        = 4;
static const uint8_t  BRK_LAD_W        = 16;   // 8 x 16 = 128, ocupa el ancho justo
static const uint8_t  BRK_LAD_H        = 5;
static const uint8_t  BRK_X0           = 0;
static const uint8_t  BRK_Y0           = 10;
static const uint8_t  BRK_TECHO_Y      = 9;
static const uint8_t  BRK_PALETA_W     = 20;
static const uint8_t  BRK_PALETA_H     = 2;
static const uint8_t  BRK_PALETA_Y     = 58;
static const float    BRK_PALETA_VEL   = 2.6f;
static const uint8_t  BRK_BOLA_LADO    = 3;
static const float    BRK_BOLA_VEL     = 1.8f;
static const float    BRK_BOLA_VEL_MAX = 3.0f;
static const float    BRK_BOLA_EFECTO  = 1.2f;
static const uint32_t BRK_SAQUE_MS     = 800;
static const uint8_t  BRK_VIDAS        = 3;

// ----- Invaders -----------------------------------------------
static const uint8_t  INV_COLS         = 6;
static const uint8_t  INV_FILAS        = 3;
static const uint8_t  INV_BICHO_W      = 7;
static const uint8_t  INV_BICHO_H      = 5;
static const uint8_t  INV_SEP_X        = 13;
static const uint8_t  INV_SEP_Y        = 9;
static const uint8_t  INV_X0           = 8;
static const uint8_t  INV_Y0           = 12;
static const uint8_t  INV_TECHO_Y      = 10;
static const float    INV_PASO_X       = 3.0f;
static const float    INV_PASO_Y       = 4.0f;
// La formación se mueve a saltos, no continuo: de ahí el andar
// entrecortado. Acelerar es solo acortar este intervalo, y se acorta solo
// a medida que quedan menos bichos.
static const uint32_t INV_PASO_MS      = 650;
static const uint32_t INV_PASO_MIN_MS  = 90;
static const uint8_t  INV_NAVE_W       = 9;
static const uint8_t  INV_NAVE_H       = 4;
static const uint8_t  INV_NAVE_Y       = 57;
static const float    INV_NAVE_VEL     = 2.2f;
static const float    INV_TIRO_VEL     = 3.5f;
static const float    INV_BOMBA_VEL    = 1.4f;
static const uint32_t INV_BOMBA_MIN_MS = 900;
static const uint32_t INV_BOMBA_MAX_MS = 2600;
static const uint8_t  INV_VIDAS        = 3;
// Cadencia del disparo automático, el modo sin palanca. Es el equivalente
// a lo rápido que podría gatillar alguien: más corto y el juego se gana
// solo, más largo y no alcanza a limpiar la formación antes de que baje.
static const uint32_t INV_AUTO_TIRO_MS = 550;

// ----- Tetris -------------------------------------------------
// Pozo de 10x15 celdas de 4 px = 40x60 px, en vertical a la izquierda.
// Los 84 px que sobran a la derecha son para puntaje, nivel y la pieza
// siguiente. Es el único juego que necesita la palanca sí o sí: con tres
// botones no entran izquierda, derecha, rotar y bajar.
static const uint8_t  TET_COLS         = 10;
static const uint8_t  TET_FILAS        = 15;
static const uint8_t  TET_CELDA        = 4;
static const uint8_t  TET_X0           = 4;
static const uint8_t  TET_Y0           = 2;
static const uint32_t TET_CAIDA_MS     = 600;
static const uint32_t TET_CAIDA_MIN_MS = 120;
static const uint8_t  TET_LINEAS_NIVEL = 5;    // líneas para subir de nivel

// ----- Laberinto (por inclinación) ----------------------------
// Grilla de 32x16 celdas de 4 px. La bolita se mueve inclinando el
// gabinete: usa el vector de gravedad del IMU, con el cero tomado al
// empezar la partida (ver imu.h sobre por qué no sirve el baseline).
// Celda de 5 px: con la bolita de 3 px quedan 2 px de juego a cada lado.
// Con celdas de 4 px el pasillo daba 1 px de margen y era exasperante.
static const uint8_t  LAB_CELDA        = 5;
static const uint8_t  LAB_COLS         = 25;
static const uint8_t  LAB_FILAS        = 11;
// Cuánta aceleración por unidad de inclinación, y cuánto se frena sola.
// El roce es lo que hace que la bolita se pueda parar; sin él, patina.
static const float    LAB_ACEL         = 0.30f;
static const float    LAB_ROCE         = 0.90f;
static const float    LAB_VEL_MAX      = 2.0f;
// Sensibilidad de la inclinación: cuántos g de desvío son "a fondo".
static const float    LAB_TILT_ESCALA  = 2.5f;
static const uint16_t LAB_TIEMPO_S     = 60;

// ----- Reacciones (duraciones en ms) --------------------------
static const uint32_t REACCION_BTN_MS    = 1500;
static const uint32_t REACCION_TOUCH_MS  = 2000;

// ----- Expresiones aleatorias durante idle --------------------
static const uint32_t GUINO_RAND_MIN_MS    = 15000;   //  15 s entre guiños
static const uint32_t GUINO_RAND_MAX_MS    = 45000;   //  45 s
static const uint32_t SOSP_RAND_MIN_MS     = 60000;   //   1 min entre sospechas
static const uint32_t SOSP_RAND_MAX_MS     = 150000;  // 2.5 min
static const uint32_t RAND_EXPR_DUR_MS     = 1500;    // duración normal
static const uint32_t QUEHACER_EXPR_DUR_MS = 4000;    // duración en modo "qué hacemos"

// ----- Inactividad y standby (pantalla apagada) ---------------
// El OLED se apaga a los 10 min sin tocar nada. Es corto a propósito: son
// píxeles orgánicos y la carita se mueve poco, así que cuanto menos tiempo
// pase mostrando una imagen casi fija, mejor para el burn-in.
//
// El "¿qué pasa?" (cara SOSPECHOSO) se dispara cada 3 min de inactividad,
// o sea a los 3, 6 y 9 min: tres avisos antes del apagado. Este intervalo
// tiene que dividir a INACTIVIDAD_STANDBY_MS en varias partes; si fueran
// iguales, el único aviso caería justo cuando la pantalla ya se apagó.
static const uint32_t INACTIVIDAD_QUEHACER_MS = 3UL  * 60UL * 1000UL;
static const uint32_t INACTIVIDAD_STANDBY_MS  = 10UL * 60UL * 1000UL; // 10 min despierto → pantalla off

// ----- Ojos: posición en pantalla -----------------------------
// Separados a propósito: el hueco entre ellos es donde vive la boca. Con
// los ojos juntos (38/90, como estaban) no quedaba lugar en el medio y la
// boca tenía que irse abajo de todo, lejos de la cara.
// El ojo más ancho de la tabla mide 30 px, así que a 30/98 los extremos
// caen en x=15 y x=113: quedan 15 px de margen a cada lado.
static const float    FACE_OJO_IZQ_CX = 30.0f;
static const float    FACE_OJO_DER_CX = 98.0f;
static const float    FACE_OJO_CY     = 35.0f;

// ----- Caras ligadas a eventos --------------------------------
// Cuánto se muestra la cara DORMIDO antes de apagar la pantalla. Es un
// aviso de que se va a dormir: sin esto el OLED se apaga de golpe y parece
// que se colgó.
static const uint32_t FACE_DORMIDO_ANTES_MS = 2200;
// Duración de la cara contenta al volver de jugar.
static const uint32_t FACE_POSTJUEGO_MS     = 2500;

// ----- Guiño --------------------------------------------------
// El guiño es un gesto con duración, no una pose: el ojo derecho baja el
// párpado, lo sostiene un instante y vuelve a abrir. El ciclo se repite
// mientras dure la expresión.
static const uint32_t GUINO_CIERRA_MS   = 130;
static const uint32_t GUINO_SOSTEN_MS   = 260;
static const uint32_t GUINO_ABRE_MS     = 170;
static const uint32_t GUINO_CICLO_MS    = 1400;  // incluye la pausa entre guiños
// Cuánto baja el párpado del ojo que guiña, y cuánto sube el ojo con él.
// La subida imita lo que hace la mejilla al cerrarse; de más queda torcido.
static const float    GUINO_PARPADO_PX  = 18.0f;
static const float    GUINO_SUBE_PX     = 4.0f;

// ----- Boca ---------------------------------------------------
// La cara arrancó siendo solo ojos. La boca se agregó después, en estilo
// kawaii (pocas líneas, mucho contraste), y este flag permite compararla
// contra la versión sin boca sin tener que revertir nada.
static const bool     FACE_BOCA_HABILITADA = true;
// Centro vertical de la boca. Va en el hueco ENTRE los ojos, a la altura
// de su parte baja — no debajo de ellos. Es lo que hace que se lea como una
// cara y no como dos ojos con algo colgando abajo.
static const float    FACE_BOCA_CY         = 44.0f;
// Grosor del trazo de los arcos, en píxeles (elipses apiladas).
// Fino a propósito: el estilo de referencia vive del contraste entre trazos
// delgados —las bocas cerradas— y manchas sólidas grandes —las abiertas—.
// Con arcos gruesos las dos mitades del repertorio pesan igual y se pierde.
static const uint8_t  FACE_BOCA_GROSOR     = 2;
// Cuánto acompaña la boca al offset de mirada. En 1.0 se mueve igual que
// los ojos y la cara entera parece girar; en 0 queda clavada al centro.
static const float    FACE_BOCA_SIGUE_MIRADA = 1.0f;
// Velocidad de la respiración propia de la boca (rad por frame). A 30 fps,
// 0.055 da un ciclo de unos 4 s: se nota que está viva sin distraer.
static const float    FACE_BOCA_VEL        = 0.055f;

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
// Recalibrados para la ventana de 10 min: con los valores viejos (bostezo
// 5-10 min, sacudida 8-15 min) la pantalla se apagaba antes de que muchos
// llegaran a dispararse una sola vez. Ahora cada gesto entra al menos una
// vez, y los tres juntos no se pisan.
static const uint32_t GESTO_BOSTEZO_MIN_MS    = 120000;  // 2 min
static const uint32_t GESTO_BOSTEZO_MAX_MS    = 240000;  // 4 min
static const uint32_t GESTO_SACUDIDA_MIN_MS   = 150000;  // 2.5 min
static const uint32_t GESTO_SACUDIDA_MAX_MS   = 330000;  // 5.5 min
static const uint32_t GESTO_MIRADA_MIN_MS     = 90000;   // 1.5 min
static const uint32_t GESTO_MIRADA_MAX_MS     = 210000;  // 3.5 min

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

// ----- Animación de encendido CRT ------------------------------
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
