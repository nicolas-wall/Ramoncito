// ============================================================
//  invaders.cpp — Space Invaders
//
//  La formación se mueve a saltos discretos y no de forma continua: es lo
//  que le da el andar entrecortado característico, y además hace que
//  acelerar sea solo acortar el intervalo entre saltos.
// ============================================================
#include "invaders.h"
#include "config.h"
#include "input.h"
#include "sound.h"
#include <math.h>

Invaders juegoInvaders;

void Invaders::begin(uint32_t now) {
    for (uint8_t f = 0; f < INV_FILAS; f++)
        for (uint8_t c = 0; c < INV_COLS; c++)
            _vivo[f][c] = true;
    _quedan = INV_FILAS * INV_COLS;

    _formX = 0; _formY = 0; _formDir = 1;
    _proximoPaso = now + INV_PASO_MS;
    _naveX = 64 - INV_NAVE_W / 2.0f;
    _tiroActivo = false;
    _bombaActiva = false;
    _proximaBomba = now + INV_BOMBA_MIN_MS;
    _proximoAuto  = now;
    _vidas = INV_VIDAS;
    _puntos = 0;
    _fin = false; _gano = false;
}

void Invaders::_disparar() {
    if (_tiroActivo) return;   // un tiro a la vez, como el original
    _tiroActivo = true;
    _tiroX = _naveX + INV_NAVE_W / 2.0f;
    _tiroY = INV_NAVE_Y - 2;
    sound.play(Melody::BIP);
}

void Invaders::_leerControles(uint32_t now) {
    float dx = input.axisX() * INV_NAVE_VEL;
    if (input.btnB()) dx -= INV_NAVE_VEL;
    if (input.btnC()) dx += INV_NAVE_VEL;
    _naveX += dx;
    if (_naveX < 0) _naveX = 0;
    if (_naveX > 128 - INV_NAVE_W) _naveX = 128 - INV_NAVE_W;

    // Disparo: el pulsador de la palanca es el botón natural. Mientras no
    // haya palanca no queda ningún botón libre —B y C están moviendo la
    // nave—, así que el cañón dispara solo, con una cadencia fija. Sin ese
    // límite volvería a disparar en cuanto el tiro anterior sale de
    // pantalla, y el juego se gana solo.
    if (input.joySw()) {
        _disparar();
    } else if (!input.joystickPresente() && (int32_t)(now - _proximoAuto) >= 0) {
        _disparar();
        _proximoAuto = now + INV_AUTO_TIRO_MS;
    }
}

bool Invaders::_colisionTiro() {
    if (!_tiroActivo) return false;
    for (uint8_t f = 0; f < INV_FILAS; f++) {
        for (uint8_t c = 0; c < INV_COLS; c++) {
            if (!_vivo[f][c]) continue;
            float bx = INV_X0 + _formX + c * INV_SEP_X;
            float by = INV_Y0 + _formY + f * INV_SEP_Y;
            if (_tiroX >= bx && _tiroX <= bx + INV_BICHO_W &&
                _tiroY >= by && _tiroY <= by + INV_BICHO_H) {
                _vivo[f][c] = false;
                _quedan--;
                // Las filas de arriba valen más: están más lejos y tardan
                // más en quedar a tiro limpio.
                _puntos += (uint16_t)((INV_FILAS - f) * 10);
                _tiroActivo = false;
                sound.play(Melody::BIP);
                return true;
            }
        }
    }
    return false;
}

void Invaders::update(uint32_t now) {
    if (_fin) return;

    _leerControles(now);

    // ── Tiro del jugador ─────────────────────────────────────
    if (_tiroActivo) {
        _tiroY -= INV_TIRO_VEL;
        if (_tiroY < INV_TECHO_Y) _tiroActivo = false;
        else _colisionTiro();
    }

    // ── Avance de la formación ───────────────────────────────
    if ((int32_t)(now - _proximoPaso) >= 0) {
        // Cuantos menos quedan, más rápido bajan. Es la aceleración
        // clásica: el último bicho es el más difícil de todos.
        uint32_t paso = INV_PASO_MS * _quedan / (INV_FILAS * INV_COLS);
        if (paso < INV_PASO_MIN_MS) paso = INV_PASO_MIN_MS;
        _proximoPaso = now + paso;

        // Bordes reales de la formación viva, para no rebotar contra
        // columnas ya vacías.
        int8_t colMin = INV_COLS, colMax = -1;
        for (uint8_t f = 0; f < INV_FILAS; f++)
            for (uint8_t c = 0; c < INV_COLS; c++)
                if (_vivo[f][c]) {
                    if (c < colMin) colMin = c;
                    if (c > colMax) colMax = c;
                }

        if (colMax < 0) { _gano = true; _fin = true; sound.play(Melody::AMOR); return; }

        float izq = INV_X0 + _formX + colMin * INV_SEP_X;
        float der = INV_X0 + _formX + colMax * INV_SEP_X + INV_BICHO_W;

        if ((_formDir > 0 && der + INV_PASO_X >= 128) ||
            (_formDir < 0 && izq - INV_PASO_X <= 0)) {
            _formDir = -_formDir;
            _formY += INV_PASO_Y;
        } else {
            _formX += _formDir * INV_PASO_X;
        }

        // ¿Llegaron abajo?
        float masBajo = 0;
        for (uint8_t f = 0; f < INV_FILAS; f++)
            for (uint8_t c = 0; c < INV_COLS; c++)
                if (_vivo[f][c]) {
                    float by = INV_Y0 + _formY + f * INV_SEP_Y + INV_BICHO_H;
                    if (by > masBajo) masBajo = by;
                }
        if (masBajo >= INV_NAVE_Y) { _fin = true; _gano = false; sound.play(Melody::ENOJADO); return; }
    }

    // ── Bombas de los bichos ─────────────────────────────────
    if (!_bombaActiva && (int32_t)(now - _proximaBomba) >= 0) {
        // Tira el bicho vivo más bajo de una columna al azar: los de atrás
        // no disparan a través de sus compañeros.
        uint8_t col = (uint8_t)(esp_random() % INV_COLS);
        int8_t filaMasBaja = -1;
        for (int8_t f = INV_FILAS - 1; f >= 0; f--)
            if (_vivo[f][col]) { filaMasBaja = f; break; }
        if (filaMasBaja >= 0) {
            _bombaActiva = true;
            _bombaX = INV_X0 + _formX + col * INV_SEP_X + INV_BICHO_W / 2.0f;
            _bombaY = INV_Y0 + _formY + filaMasBaja * INV_SEP_Y + INV_BICHO_H;
        }
        _proximaBomba = now + INV_BOMBA_MIN_MS +
                        (esp_random() % (INV_BOMBA_MAX_MS - INV_BOMBA_MIN_MS));
    }

    if (_bombaActiva) {
        _bombaY += INV_BOMBA_VEL;
        if (_bombaY > 64) {
            _bombaActiva = false;
        } else if (_bombaY >= INV_NAVE_Y &&
                   _bombaX >= _naveX && _bombaX <= _naveX + INV_NAVE_W) {
            _bombaActiva = false;
            if (_vidas > 0) _vidas--;
            sound.play(Melody::TRISTE);
            if (_vidas == 0) { _fin = true; _gano = false; }
        }
    }
}

void Invaders::render(U8G2& u8) {
    char buf[16];
    u8.setFont(u8g2_font_5x7_tf);
    snprintf(buf, sizeof(buf), "%u", _puntos);
    u8.drawStr(2, 7, buf);
    for (uint8_t i = 0; i < _vidas; i++) u8.drawBox(120 - i * 5, 2, 3, 3);
    u8.drawHLine(0, INV_TECHO_Y - 1, 128);

    // Bichos: un cuerpo con dos "patas", suficiente para leerse a 7x5
    for (uint8_t f = 0; f < INV_FILAS; f++) {
        for (uint8_t c = 0; c < INV_COLS; c++) {
            if (!_vivo[f][c]) continue;
            int x = (int)(INV_X0 + _formX + c * INV_SEP_X);
            int y = (int)(INV_Y0 + _formY + f * INV_SEP_Y);
            u8.drawBox(x + 1, y, INV_BICHO_W - 2, INV_BICHO_H - 2);
            u8.drawPixel(x, y + INV_BICHO_H - 1);
            u8.drawPixel(x + INV_BICHO_W - 1, y + INV_BICHO_H - 1);
        }
    }

    // Cañón: base ancha con torreta
    int nx = (int)_naveX;
    u8.drawBox(nx, INV_NAVE_Y + 2, INV_NAVE_W, INV_NAVE_H - 2);
    u8.drawBox(nx + INV_NAVE_W / 2 - 1, INV_NAVE_Y, 2, 2);

    if (_tiroActivo)  u8.drawVLine((int)_tiroX, (int)_tiroY, 3);
    if (_bombaActiva) u8.drawVLine((int)_bombaX, (int)_bombaY, 3);
}
