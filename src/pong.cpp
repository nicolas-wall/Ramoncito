// ============================================================
//  pong.cpp — Pong contra la CPU
// ============================================================
#include "pong.h"
#include "config.h"
#include "input.h"
#include "sound.h"
#include <math.h>

Pong juegoPong;

void Pong::begin(uint32_t now) {
    _ptsJug = _ptsCpu = 0;
    _palJug = (PONG_TOP_Y + PONG_BOT_Y) / 2.0f - PONG_PALETA_H / 2.0f;
    _palCpu = _palJug;
    _fin = false;
    _ganoJug = false;
    _sacar(now, true);
}

void Pong::_sacar(uint32_t now, bool haciaJug) {
    // La pelota nace en el centro y queda congelada hasta que vence la pausa
    // de saque: da tiempo a reubicar la paleta después de un punto.
    _bolaX = 128 / 2.0f - PONG_BOLA_LADO / 2.0f;
    _bolaY = (PONG_TOP_Y + PONG_BOT_Y) / 2.0f - PONG_BOLA_LADO / 2.0f;
    _velX  = haciaJug ? -PONG_BOLA_VEL_INI : PONG_BOLA_VEL_INI;
    // Componente vertical al azar pero nunca cero: un saque horizontal puro
    // se vuelve un peloteo idéntico e infinito.
    float vy = (float)(esp_random() % 100) / 100.0f - 0.5f;
    if (fabsf(vy) < 0.15f) vy = (vy < 0) ? -0.15f : 0.15f;
    _velY = vy * PONG_BOLA_VEL_INI;
    _saqueHasta = now + PONG_SAQUE_MS;
}

void Pong::update(uint32_t now) {
    if (_fin) return;

    // ── Paleta del jugador ───────────────────────────────────
    // El eje analógico y los botones se suman: así anda con la palanca, con
    // B/C y con el stub por serial, sin ramas separadas.
    float dy = input.axisY() * PONG_PALETA_VEL;
    if (input.btnB()) dy -= PONG_PALETA_VEL;
    if (input.btnC()) dy += PONG_PALETA_VEL;

    _palJug += dy;
    if (_palJug < PONG_TOP_Y) _palJug = PONG_TOP_Y;
    if (_palJug > PONG_BOT_Y - PONG_PALETA_H) _palJug = PONG_BOT_Y - PONG_PALETA_H;

    // ── Paleta de la CPU ─────────────────────────────────────
    // Persigue la pelota solo cuando viene hacia ella; si va para el otro
    // lado vuelve al centro. Eso la hace parecer que "espera" en vez de
    // copiar la trayectoria, y de paso le da al jugador su ventana.
    float objetivo = (_velX > 0) ? (_bolaY + PONG_BOLA_LADO / 2.0f)
                                 : (PONG_TOP_Y + PONG_BOT_Y) / 2.0f;
    float centroCpu = _palCpu + PONG_PALETA_H / 2.0f;
    float dif = objetivo - centroCpu;
    if (fabsf(dif) > PONG_CPU_ZONA_MUERTA) _palCpu += (dif > 0) ? PONG_CPU_VEL : -PONG_CPU_VEL;
    if (_palCpu < PONG_TOP_Y) _palCpu = PONG_TOP_Y;
    if (_palCpu > PONG_BOT_Y - PONG_PALETA_H) _palCpu = PONG_BOT_Y - PONG_PALETA_H;

    // ── Pausa de saque: las paletas se mueven, la pelota no ──
    if (_saqueHasta != 0) {
        if ((int32_t)(now - _saqueHasta) < 0) return;
        _saqueHasta = 0;
    }

    // ── Pelota ───────────────────────────────────────────────
    _bolaX += _velX;
    _bolaY += _velY;

    if (_bolaY < PONG_TOP_Y) {
        _bolaY = PONG_TOP_Y;  _velY = -_velY;  sound.play(Melody::BIP);
    } else if (_bolaY + PONG_BOLA_LADO > PONG_BOT_Y) {
        _bolaY = PONG_BOT_Y - PONG_BOLA_LADO;  _velY = -_velY;  sound.play(Melody::BIP);
    }

    // Paleta del jugador (izquierda)
    if (_velX < 0 &&
        _bolaX <= PONG_PALETA_X_JUG + PONG_PALETA_W &&
        _bolaX + PONG_BOLA_LADO >= PONG_PALETA_X_JUG &&
        _bolaY + PONG_BOLA_LADO >= _palJug && _bolaY <= _palJug + PONG_PALETA_H) {

        _bolaX = PONG_PALETA_X_JUG + PONG_PALETA_W;
        _velX  = -_velX;
        // Efecto según dónde pegó respecto del centro: el borde manda la
        // pelota en diagonal, el centro la devuelve plana.
        float rel = ((_bolaY + PONG_BOLA_LADO / 2.0f) -
                     (_palJug + PONG_PALETA_H / 2.0f)) / (PONG_PALETA_H / 2.0f);
        _velY += rel * PONG_BOLA_EFECTO;
        // El tope se compara sobre la MAGNITUD: velX cambia de signo en cada
        // paleta, y comparar el valor con signo deja pasar el límite.
        if (fabsf(_velX) < PONG_BOLA_VEL_MAX) _velX += PONG_BOLA_ACEL;
        sound.play(Melody::BIP);
    }

    // Paleta de la CPU (derecha)
    if (_velX > 0 &&
        _bolaX + PONG_BOLA_LADO >= PONG_PALETA_X_CPU &&
        _bolaX <= PONG_PALETA_X_CPU + PONG_PALETA_W &&
        _bolaY + PONG_BOLA_LADO >= _palCpu && _bolaY <= _palCpu + PONG_PALETA_H) {

        _bolaX = PONG_PALETA_X_CPU - PONG_BOLA_LADO;
        _velX  = -_velX;
        float rel = ((_bolaY + PONG_BOLA_LADO / 2.0f) -
                     (_palCpu + PONG_PALETA_H / 2.0f)) / (PONG_PALETA_H / 2.0f);
        _velY += rel * PONG_BOLA_EFECTO;
        if (fabsf(_velX) < PONG_BOLA_VEL_MAX) _velX -= PONG_BOLA_ACEL;
        sound.play(Melody::BIP);
    }

    // Techo vertical: sin esto el efecto se acumula rebote a rebote y la
    // pelota termina viajando casi en vertical.
    if (_velY >  PONG_BOLA_VEL_MAX) _velY =  PONG_BOLA_VEL_MAX;
    if (_velY < -PONG_BOLA_VEL_MAX) _velY = -PONG_BOLA_VEL_MAX;

    // ── Puntos ───────────────────────────────────────────────
    bool punto = false;
    if (_bolaX + PONG_BOLA_LADO < 0) {
        _ptsCpu++; punto = true; sound.play(Melody::TRISTE); _sacar(now, false);
    } else if (_bolaX > 128) {
        _ptsJug++; punto = true; sound.play(Melody::FELIZ);  _sacar(now, true);
    }

    if (punto) {
        Serial.printf("[pong] %u - %u\n", _ptsJug, _ptsCpu);
        if (_ptsJug >= PONG_PUNTOS_GANAR || _ptsCpu >= PONG_PUNTOS_GANAR) {
            _ganoJug = (_ptsJug > _ptsCpu);
            _fin = true;
        }
    }
}

void Pong::render(U8G2& u8) {
    char buf[8];
    u8.setFont(u8g2_font_7x13B_tf);
    snprintf(buf, sizeof(buf), "%u", _ptsJug);
    u8.drawStr(52 - u8.getStrWidth(buf), 10, buf);
    snprintf(buf, sizeof(buf), "%u", _ptsCpu);
    u8.drawStr(76, 10, buf);
    u8.setFont(u8g2_font_5x7_tf);
    u8.drawStr(60, 10, "-");

    for (int y = PONG_TOP_Y; y < PONG_BOT_Y; y += 5) u8.drawVLine(64, y, 3);

    u8.drawBox(PONG_PALETA_X_JUG, (int)_palJug, PONG_PALETA_W, PONG_PALETA_H);
    u8.drawBox(PONG_PALETA_X_CPU, (int)_palCpu, PONG_PALETA_W, PONG_PALETA_H);

    // Durante la pausa de saque la pelota titila, para que se vea que va a salir
    bool mostrar = (_saqueHasta == 0) || (((millis() / 150) % 2) == 0);
    if (mostrar) u8.drawBox((int)_bolaX, (int)_bolaY, PONG_BOLA_LADO, PONG_BOLA_LADO);
}
