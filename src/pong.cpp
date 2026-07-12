// =============================================================
//  pong.cpp — Minijuego Pong oculto (doc 04-PONG.md)
//  Plataforma: ESP32-S3 / PlatformIO / Arduino
//  OLED 128x64, U8g2. Loop a 30 fps, sin delays ni malloc.
// =============================================================

#include "pong.h"
#include "sound.h"
#include "config.h"
#include <math.h>

// ---- Instancia global ----------------------------------------
Pong pong;

// ---- Constantes de campo ------------------------------------

// Área de juego vertical (filas 9-63, 55 px de alto)
static const float CAMPO_TOP    =  9.0f;
static const float CAMPO_BOT    = 63.0f;

// Paleta: dimensiones
static const float PALETA_W     =  3.0f;
static const float PALETA_H     = 12.0f;

// Posición X de las paletas (borde interior)
static const float PALETA_JUG_X =  4.0f;   // jugador izquierda
static const float PALETA_CPU_X = 121.0f;  // CPU derecha

// Radio de la pelota
static const float BOLA_R       =  2.0f;

// Velocidad inicial de la pelota
static const float VX0          =  2.5f;
static const float VY0          =  1.2f;

// Aceleración por rebote en paleta y tope de velocidad
static const float ACCEL_REBOTE =  1.06f;
static const float MAX_SPEED    =  6.0f;

// Ángulo máximo de rebote en paleta (60° en radianes)
static const float MAX_BOUNCE   =  60.0f * (float)M_PI / 180.0f;

// Velocidad del jugador (px/frame) y clamp al campo
static const float JUG_SPEED   =  2.5f;

// Puntos para ganar
static const uint8_t PUNTOS_WIN = 5;

// Duración de la pantalla de resultado (ms)
static const uint32_t RESULT_MS = 2000;

// ---- Tabla de dificultad (por puntos del jugador) -----------
// Índice 0-4 = puntos del jugador 0-4; índice 5+ usa valor [4]
static const float CPU_BASE_SPEED[]  = { 1.8f, 2.1f, 2.4f, 2.7f, 3.0f };
static const float CPU_ERROR_RANGE[] = { 10.0f, 8.0f, 6.0f, 4.0f, 2.0f };
static const uint8_t DIFF_TABLA_SIZE = 5;

// ---- Helpers internos ----------------------------------------

// Clamp de float
static inline float fclamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// ---- Implementación ------------------------------------------

void Pong::enter() {
    _activo          = true;
    _terminado       = false;
    _resultado       = PongResult::EN_JUEGO;
    _puntosJugador   = 0;
    _puntosCPU       = 0;

    // Paleta jugador centrada en el campo
    _pyY  = CAMPO_TOP + (CAMPO_BOT - CAMPO_TOP - PALETA_H) / 2.0f;
    _cpuY = _pyY;

    // IA: error inicial en 0
    _cpuOffset         = 0.0f;
    _cpuOffsetTarget   = 0.0f;
    _cpuOffsetTimer    = 0;
    _cpuOffsetInterval = 60;

    _colisionadoIzq = false;
    _colisionadoDer = false;

    // Jingle de inicio
    sound.play(Melody::BOOT);

    // Saque inicial hacia el jugador
    _resetBola(true);
}

void Pong::exit() {
    _activo    = false;
    _terminado = false;
}

// Devuelve parámetros de dificultad según puntos actuales del jugador
void Pong::_getDificultad(float &baseSpeed, float &errorRange) const {
    uint8_t idx = (_puntosJugador < DIFF_TABLA_SIZE) ? _puntosJugador : (DIFF_TABLA_SIZE - 1);
    baseSpeed  = CPU_BASE_SPEED[idx];
    errorRange = CPU_ERROR_RANGE[idx];
}

// Reposiciona la pelota en el centro y le da velocidad inicial
// haciaJugador=true → la pelota va hacia la izquierda (hacia quien recibió)
void Pong::_resetBola(bool haciaJugador) {
    _bx  = 64.0f; // centro horizontal
    _by  = (CAMPO_TOP + CAMPO_BOT) / 2.0f;

    // Dirección horizontal: hacia quien recibió el punto
    float dirX = haciaJugador ? -1.0f : 1.0f;

    // Pequeño ángulo vertical aleatorio para no sacar siempre horizontal puro
    // Usamos un valor pseudo-aleatorio acotado (±VY0)
    float vy = (float)(esp_random() % 100) / 100.0f * VY0 * 2.0f - VY0;

    _bvx = dirX * VX0;
    _bvy = vy;

    // Rearmar flags anti-doble-rebote
    _colisionadoIzq = false;
    _colisionadoDer = false;
}

// Movimiento de la IA con error aleatorio y dificultad progresiva
void Pong::_actualizarIA() {
    float baseSpeed, errorRange;
    _getDificultad(baseSpeed, errorRange);

    // Contar frames para renovar el offset de error
    _cpuOffsetTimer--;
    if (_cpuOffsetTimer <= 0) {
        // Nuevo intervalo: 40-80 frames
        _cpuOffsetInterval = 40 + (int16_t)(esp_random() % 41);
        _cpuOffsetTimer    = _cpuOffsetInterval;

        // Nuevo offset objetivo en [-errorRange, +errorRange]
        if (errorRange > 0.0f) {
            float r = (float)(esp_random() % 1000) / 1000.0f; // [0, 1)
            _cpuOffsetTarget = (r * 2.0f - 1.0f) * errorRange;
        } else {
            _cpuOffsetTarget = 0.0f;
        }
    }

    // El offset decae linealmente hacia el target durante el intervalo
    float t = 1.0f - (float)_cpuOffsetTimer / (float)_cpuOffsetInterval;
    _cpuOffset = _cpuOffsetTarget * t;

    // Target de la CPU: pelota + error
    float cpuCenter = _cpuY + PALETA_H / 2.0f;
    float targetY   = _by + _cpuOffset;
    float diff      = targetY - cpuCenter;

    if (fabsf(diff) > 1.0f) {
        float move = fclamp(diff, -baseSpeed, baseSpeed);
        _cpuY += move;
    }

    // Clamp al área de juego
    _cpuY = fclamp(_cpuY, CAMPO_TOP, CAMPO_BOT - PALETA_H);
}

void Pong::update(uint32_t now, bool btnArriba, bool btnAbajo) {
    if (!_activo) return;

    // Si ya hay resultado, esperar los 2 s y marcar como terminado
    if (_resultado != PongResult::EN_JUEGO) {
        if ((int32_t)(now - _resultHasta) >= 0) {
            _terminado = true;
        }
        return;
    }

    // ---- Mover paleta del jugador --------------------------------
    if (btnArriba) _pyY -= JUG_SPEED;
    if (btnAbajo)  _pyY += JUG_SPEED;
    _pyY = fclamp(_pyY, CAMPO_TOP, CAMPO_BOT - PALETA_H);

    // ---- Mover IA ------------------------------------------------
    _actualizarIA();

    // ---- Mover pelota --------------------------------------------
    _bx += _bvx;
    _by += _bvy;

    // ---- Rebote en techo -----------------------------------------
    if (_by - BOLA_R < CAMPO_TOP) {
        _by  = CAMPO_TOP + BOLA_R;
        _bvy = fabsf(_bvy);
        sound.play(Melody::BIP);
    }

    // ---- Rebote en suelo -----------------------------------------
    if (_by + BOLA_R > CAMPO_BOT) {
        _by  = CAMPO_BOT - BOLA_R;
        _bvy = -fabsf(_bvy);
        sound.play(Melody::BIP);
    }

    // ---- Colisión con paleta del jugador (izquierda) -------------
    // AABB: pelota vs rect de la paleta. Solo si la pelota va hacia la izquierda.
    if (_bvx < 0.0f && !_colisionadoIzq) {
        float bLeft  = _bx - BOLA_R;
        float bRight = _bx + BOLA_R;
        float bTop   = _by - BOLA_R;
        float bBot   = _by + BOLA_R;

        float pLeft  = PALETA_JUG_X;
        float pRight = PALETA_JUG_X + PALETA_W;
        float pTop   = _pyY;
        float pBot   = _pyY + PALETA_H;

        if (bRight >= pLeft && bLeft <= pRight &&
            bBot  >= pTop  && bTop  <= pBot) {

            // Punto de impacto normalizado [-1, 1]
            float hitPos     = (_by - (_pyY + PALETA_H / 2.0f)) / (PALETA_H / 2.0f);
            hitPos           = fclamp(hitPos, -1.0f, 1.0f);
            float angle      = hitPos * MAX_BOUNCE;

            // Nueva velocidad con aceleración +6%
            float speed = sqrtf(_bvx * _bvx + _bvy * _bvy) * ACCEL_REBOTE;
            if (speed > MAX_SPEED) speed = MAX_SPEED;

            // Pelota izquierda: sale hacia la derecha
            _bvx = speed * cosf(angle);
            _bvy = speed * sinf(angle);

            // Asegurar que realmente se aleja
            if (_bvx < 0.0f) _bvx = -_bvx;

            _colisionadoIzq = true;
            _colisionadoDer = false; // puede volver a rebotar en CPU
            sound.play(Melody::BIP);
        }
    }

    // ---- Colisión con paleta CPU (derecha) -----------------------
    // Solo si la pelota va hacia la derecha.
    if (_bvx > 0.0f && !_colisionadoDer) {
        float bLeft  = _bx - BOLA_R;
        float bRight = _bx + BOLA_R;
        float bTop   = _by - BOLA_R;
        float bBot   = _by + BOLA_R;

        float pLeft  = PALETA_CPU_X;
        float pRight = PALETA_CPU_X + PALETA_W;
        float pTop   = _cpuY;
        float pBot   = _cpuY + PALETA_H;

        if (bRight >= pLeft && bLeft <= pRight &&
            bBot  >= pTop  && bTop  <= pBot) {

            float hitPos     = (_by - (_cpuY + PALETA_H / 2.0f)) / (PALETA_H / 2.0f);
            hitPos           = fclamp(hitPos, -1.0f, 1.0f);
            float angle      = hitPos * MAX_BOUNCE;

            float speed = sqrtf(_bvx * _bvx + _bvy * _bvy) * ACCEL_REBOTE;
            if (speed > MAX_SPEED) speed = MAX_SPEED;

            // Paleta derecha: sale hacia la izquierda
            _bvx = -speed * cosf(angle);
            _bvy =  speed * sinf(angle);

            // Asegurar que realmente se aleja
            if (_bvx > 0.0f) _bvx = -_bvx;

            _colisionadoDer = true;
            _colisionadoIzq = false;
            sound.play(Melody::BIP);
        }
    }

    // ---- Pelota sale por la izquierda → punto para la CPU --------
    if (_bx + BOLA_R < 0.0f) {
        _puntosCPU++;
        sound.play(Melody::SORPRESA);

        if (_puntosCPU >= PUNTOS_WIN) {
            _resultado   = PongResult::GANA_CPU;
            _resultHasta = millis() + RESULT_MS;
        } else {
            // Saque hacia el jugador (quien recibió el punto)
            _resetBola(true);
        }
    }

    // ---- Pelota sale por la derecha → punto para el jugador ------
    if (_bx - BOLA_R > 127.0f) {
        _puntosJugador++;
        sound.play(Melody::SORPRESA);

        if (_puntosJugador >= PUNTOS_WIN) {
            _resultado   = PongResult::GANA_JUGADOR;
            _resultHasta = millis() + RESULT_MS;
        } else {
            // Saque hacia la CPU (quien recibió el punto)
            _resetBola(false);
        }
    }
}

void Pong::render(U8G2 &u8) {
    if (!_activo) return;

    // ---- Marcador (zona y=0..8, centrado) ------------------------
    char scoreStr[16];
    // Formato: "N  :  M"  (jugador : CPU)
    snprintf(scoreStr, sizeof(scoreStr), "%u  :  %u", _puntosJugador, _puntosCPU);

    u8.setFont(u8g2_font_tiny5_tf);
    // Centrar el string: ancho de la pantalla 128 px
    int16_t strW = u8.getStrWidth(scoreStr);
    int16_t sx   = (128 - strW) / 2;
    u8.drawStr(sx, 8, scoreStr); // y=8 → baseline encaja en los 9 px del marcador

    // ---- Línea divisoria central (guiones, opcional estético) ----
    // Dibujar puntos cada 4 px en x=64
    for (int8_t yy = 9; yy < 64; yy += 4) {
        u8.drawPixel(64, yy);
    }

    // ---- Paletas -------------------------------------------------
    u8.drawBox((int16_t)PALETA_JUG_X, (int16_t)_pyY,  (int16_t)PALETA_W, (int16_t)PALETA_H);
    u8.drawBox((int16_t)PALETA_CPU_X, (int16_t)_cpuY, (int16_t)PALETA_W, (int16_t)PALETA_H);

    // ---- Pelota --------------------------------------------------
    u8.drawDisc((int16_t)_bx, (int16_t)_by, (uint8_t)BOLA_R);

    // ---- Pantalla de resultado (si ya terminó la partida) --------
    if (_resultado != PongResult::EN_JUEGO) {
        // Cuadro de resultado semi-centrado
        char linea1[24], linea2[24];

        if (_resultado == PongResult::GANA_JUGADOR) {
            snprintf(linea1, sizeof(linea1), "GANASTE %u-%u", _puntosJugador, _puntosCPU);
            snprintf(linea2, sizeof(linea2), "Bien jugado!");
        } else {
            snprintf(linea1, sizeof(linea1), "GANE YO! %u-%u", _puntosCPU, _puntosJugador);
            snprintf(linea2, sizeof(linea2), "Soy invencible");
        }

        // Fondo negro para el cuadro de resultado
        u8.setDrawColor(0);
        u8.drawBox(10, 22, 108, 26);
        u8.setDrawColor(1);
        u8.drawFrame(10, 22, 108, 26);

        u8.setFont(u8g2_font_5x7_tf);
        int16_t w1 = u8.getStrWidth(linea1);
        int16_t w2 = u8.getStrWidth(linea2);
        u8.drawStr((128 - w1) / 2, 33, linea1);
        u8.drawStr((128 - w2) / 2, 44, linea2);
    }
}

PongResult Pong::result() const {
    return _resultado;
}

bool Pong::done() const {
    return _terminado;
}
