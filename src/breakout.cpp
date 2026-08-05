// ============================================================
//  breakout.cpp — Breakout
// ============================================================
#include "breakout.h"
#include "config.h"
#include "input.h"
#include "sound.h"
#include <math.h>

Breakout juegoBreakout;

void Breakout::begin(uint32_t now) {
    for (uint8_t f = 0; f < BRK_FILAS; f++)
        for (uint8_t c = 0; c < BRK_COLS; c++)
            _ladrillo[f][c] = true;
    _quedan = BRK_FILAS * BRK_COLS;

    _paleta = 64 - BRK_PALETA_W / 2.0f;
    _vidas  = BRK_VIDAS;
    _puntos = 0;
    _fin = false; _gano = false;
    _sacar(now);
}

void Breakout::_sacar(uint32_t now) {
    _bolaX = _paleta + BRK_PALETA_W / 2.0f - BRK_BOLA_LADO / 2.0f;
    _bolaY = BRK_PALETA_Y - BRK_BOLA_LADO - 1;
    // Sale siempre hacia arriba, con una inclinación al azar que no llega a
    // ser vertical: una pelota subiendo derecho rebota eternamente en la
    // misma columna sin romper casi nada.
    float vx = (float)(esp_random() % 100) / 100.0f - 0.5f;
    if (fabsf(vx) < 0.2f) vx = (vx < 0) ? -0.2f : 0.2f;
    _velX = vx * 2.0f * BRK_BOLA_VEL;
    _velY = -BRK_BOLA_VEL;
    _saqueHasta = now + BRK_SAQUE_MS;
}

// Invierte la componente correcta según por dónde entró la pelota al
// ladrillo. Se decide comparando cuánto se metió en cada eje: el menor
// solapamiento es la cara por la que golpeó.
void Breakout::_rebotarLadrillo(int8_t col, int8_t fila) {
    float bx = BRK_X0 + col * BRK_LAD_W;
    float by = BRK_Y0 + fila * BRK_LAD_H;
    float cxBola = _bolaX + BRK_BOLA_LADO / 2.0f;
    float cyBola = _bolaY + BRK_BOLA_LADO / 2.0f;
    float solapeX = (BRK_LAD_W / 2.0f + BRK_BOLA_LADO / 2.0f) - fabsf(cxBola - (bx + BRK_LAD_W / 2.0f));
    float solapeY = (BRK_LAD_H / 2.0f + BRK_BOLA_LADO / 2.0f) - fabsf(cyBola - (by + BRK_LAD_H / 2.0f));
    if (solapeX < solapeY) _velX = -_velX;
    else                   _velY = -_velY;
}

void Breakout::update(uint32_t now) {
    if (_fin) return;

    // ── Paleta ───────────────────────────────────────────────
    float dx = input.axisX() * BRK_PALETA_VEL;
    if (input.btnB()) dx -= BRK_PALETA_VEL;
    if (input.btnC()) dx += BRK_PALETA_VEL;
    _paleta += dx;
    if (_paleta < 0) _paleta = 0;
    if (_paleta > 128 - BRK_PALETA_W) _paleta = 128 - BRK_PALETA_W;

    // ── Saque: la pelota viaja pegada a la paleta ────────────
    if (_saqueHasta != 0) {
        _bolaX = _paleta + BRK_PALETA_W / 2.0f - BRK_BOLA_LADO / 2.0f;
        if ((int32_t)(now - _saqueHasta) < 0) return;
        _saqueHasta = 0;
    }

    _bolaX += _velX;
    _bolaY += _velY;

    // ── Paredes ──────────────────────────────────────────────
    if (_bolaX < 0) { _bolaX = 0; _velX = -_velX; sound.play(Melody::BIP); }
    if (_bolaX + BRK_BOLA_LADO > 128) { _bolaX = 128 - BRK_BOLA_LADO; _velX = -_velX; sound.play(Melody::BIP); }
    if (_bolaY < BRK_TECHO_Y) { _bolaY = BRK_TECHO_Y; _velY = -_velY; sound.play(Melody::BIP); }

    // ── Paleta ───────────────────────────────────────────────
    if (_velY > 0 &&
        _bolaY + BRK_BOLA_LADO >= BRK_PALETA_Y &&
        _bolaY <= BRK_PALETA_Y + BRK_PALETA_H &&
        _bolaX + BRK_BOLA_LADO >= _paleta && _bolaX <= _paleta + BRK_PALETA_W) {

        _bolaY = BRK_PALETA_Y - BRK_BOLA_LADO;
        _velY  = -_velY;
        float rel = ((_bolaX + BRK_BOLA_LADO / 2.0f) -
                     (_paleta + BRK_PALETA_W / 2.0f)) / (BRK_PALETA_W / 2.0f);
        _velX += rel * BRK_BOLA_EFECTO;
        if (_velX >  BRK_BOLA_VEL_MAX) _velX =  BRK_BOLA_VEL_MAX;
        if (_velX < -BRK_BOLA_VEL_MAX) _velX = -BRK_BOLA_VEL_MAX;
        sound.play(Melody::BIP);
    }

    // ── Ladrillos ────────────────────────────────────────────
    // Se mira la celda donde cae el centro de la pelota. Con la grilla
    // alineada esto es exacto y no se puede colar entre dos ladrillos, que
    // es lo que pasaría comparando cajas a 3 px por frame.
    int8_t col  = (int8_t)((_bolaX + BRK_BOLA_LADO / 2.0f - BRK_X0) / BRK_LAD_W);
    int8_t fila = (int8_t)((_bolaY + BRK_BOLA_LADO / 2.0f - BRK_Y0) / BRK_LAD_H);
    if (col >= 0 && col < BRK_COLS && fila >= 0 && fila < BRK_FILAS && _ladrillo[fila][col]) {
        _ladrillo[fila][col] = false;
        _quedan--;
        // Las filas de arriba valen más: premia arriesgarse a subir.
        _puntos += (uint16_t)(BRK_FILAS - fila);
        _rebotarLadrillo(col, fila);
        sound.play(Melody::BIP);
        if (_quedan == 0) {
            _gano = true; _fin = true;
            sound.play(Melody::AMOR);
            return;
        }
    }

    // ── Se cayó ──────────────────────────────────────────────
    if (_bolaY > 64) {
        if (_vidas > 0) _vidas--;
        sound.play(Melody::TRISTE);
        if (_vidas == 0) { _fin = true; _gano = false; }
        else             _sacar(now);
    }
}

void Breakout::render(U8G2& u8) {
    char buf[16];
    u8.setFont(u8g2_font_5x7_tf);
    snprintf(buf, sizeof(buf), "%u", _puntos);
    u8.drawStr(2, 7, buf);
    // Vidas como cuadraditos arriba a la derecha
    for (uint8_t i = 0; i < _vidas; i++) u8.drawBox(120 - i * 5, 2, 3, 3);

    for (uint8_t f = 0; f < BRK_FILAS; f++)
        for (uint8_t c = 0; c < BRK_COLS; c++)
            if (_ladrillo[f][c])
                u8.drawBox(BRK_X0 + c * BRK_LAD_W, BRK_Y0 + f * BRK_LAD_H,
                           BRK_LAD_W - 1, BRK_LAD_H - 1);

    u8.drawBox((int)_paleta, BRK_PALETA_Y, BRK_PALETA_W, BRK_PALETA_H);

    bool mostrar = (_saqueHasta == 0) || (((millis() / 150) % 2) == 0);
    if (mostrar) u8.drawBox((int)_bolaX, (int)_bolaY, BRK_BOLA_LADO, BRK_BOLA_LADO);
}
