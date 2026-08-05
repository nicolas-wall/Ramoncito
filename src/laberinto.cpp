// ============================================================
//  laberinto.cpp — Laberinto por inclinación (IMU)
//
//  El laberinto es un dibujo en texto: 11 filas de 25 caracteres, una
//  celda de 5 px cada uno.
//    '#' pared    '.' piso    'S' salida (llegada)
//
//  Se puede editar a mano, pero conviene regenerarlo con
//  tools/gen-laberinto.py: ese script verifica por inundación que la meta
//  sea alcanzable. El primer mapa de este juego se dibujó a mano y tenía la
//  salida aislada — algo que no se ve leyendo el código ni compilando.
// ============================================================
#include "laberinto.h"
#include "config.h"
#include "input.h"
#include "imu.h"
#include "sound.h"
#include <math.h>

Laberinto juegoLaberinto;

static const char* const MAPA[LAB_FILAS] = {
    "#########################",
    "#.....#.......#...#.....#",
    "#####.#.#####.###.#.#.#.#",
    "#.....#...#.#.#...#...#.#",
    "#.##.####.#.#.#.#####.#.#",
    "#.#.......#.#.#.......#.#",
    "#.#.###.###.#.#.#######.#",
    "#.#...#.#...#.#.......#.#",
    "#.#####.#.###.#########.#",
    "#.......#..............S#",
    "#########################",
};

// Entrada arriba a la izquierda; la salida ('S') está en la esquina
// opuesta. El mapa lo genera tools/gen-laberinto.py, que verifica por
// inundación que la meta sea alcanzable antes de escribirlo: un laberinto
// sin solución es un juego roto y no se nota hasta jugarlo.
static const uint8_t INICIO_COL = 1;
static const uint8_t INICIO_FILA = 1;

void Laberinto::begin(uint32_t now) {
    _x = INICIO_COL * LAB_CELDA + LAB_CELDA / 2.0f;
    _y = INICIO_FILA * LAB_CELDA + LAB_CELDA / 2.0f;
    _vx = _vy = 0;
    // Tomar el cero de inclinación en la posición en que esté el aparato
    // ahora: se juega inclinando DESDE acá, no desde la horizontal.
    _cero1 = imu.habilitado() ? imu.gravY() : 0.0f;
    _cero2 = imu.habilitado() ? imu.gravZ() : 0.0f;
    _finTiempo = now + (uint32_t)LAB_TIEMPO_S * 1000UL;
    _fin = false; _gano = false;
    _puntosFinales = 0;
}

// ¿El punto (px,py) cae en una pared?
bool Laberinto::_pared(float px, float py) const {
    if (px < 0 || py < 0) return true;
    int c = (int)(px / LAB_CELDA);
    int f = (int)(py / LAB_CELDA);
    if (c < 0 || c >= LAB_COLS || f < 0 || f >= LAB_FILAS) return true;
    return MAPA[f][c] == '#';
}

void Laberinto::_leerInclinacion(float& ax, float& ay) {
    if (imu.habilitado()) {
        // El eje X del MPU es el que apunta hacia abajo en este montaje, así
        // que los dos ejes horizontales del juego son Y y Z.
        ax = (imu.gravY() - _cero1) * LAB_TILT_ESCALA;
        ay = (imu.gravZ() - _cero2) * LAB_TILT_ESCALA;
    } else {
        // Sin acelerómetro el juego igual se puede jugar con la palanca.
        ax = input.axisX();
        ay = input.axisY();
    }
    if (ax >  1.0f) ax =  1.0f;   if (ax < -1.0f) ax = -1.0f;
    if (ay >  1.0f) ay =  1.0f;   if (ay < -1.0f) ay = -1.0f;
}

void Laberinto::update(uint32_t now) {
    if (_fin) return;

    if ((int32_t)(now - _finTiempo) >= 0) {
        _fin = true; _gano = false; _puntosFinales = 0;
        sound.play(Melody::TRISTE);
        return;
    }

    float ax, ay;
    _leerInclinacion(ax, ay);

    _vx = (_vx + ax * LAB_ACEL) * LAB_ROCE;
    _vy = (_vy + ay * LAB_ACEL) * LAB_ROCE;
    if (_vx >  LAB_VEL_MAX) _vx =  LAB_VEL_MAX;
    if (_vx < -LAB_VEL_MAX) _vx = -LAB_VEL_MAX;
    if (_vy >  LAB_VEL_MAX) _vy =  LAB_VEL_MAX;
    if (_vy < -LAB_VEL_MAX) _vy = -LAB_VEL_MAX;

    // Se mueve un eje por vez y se revierte solo el que chocó. Moviendo los
    // dos juntos, rozar una pared en diagonal frenaría las dos componentes
    // y la bolita se pegaría en las esquinas.
    const float r = 1.0f;   // medio ancho de la bolita
    float nx = _x + _vx;
    if (_pared(nx - r, _y - r) || _pared(nx + r, _y - r) ||
        _pared(nx - r, _y + r) || _pared(nx + r, _y + r)) {
        _vx = 0;
    } else {
        _x = nx;
    }

    float ny = _y + _vy;
    if (_pared(_x - r, ny - r) || _pared(_x + r, ny - r) ||
        _pared(_x - r, ny + r) || _pared(_x + r, ny + r)) {
        _vy = 0;
    } else {
        _y = ny;
    }

    // ¿Llegó?
    int c = (int)(_x / LAB_CELDA), f = (int)(_y / LAB_CELDA);
    if (c >= 0 && c < LAB_COLS && f >= 0 && f < LAB_FILAS && MAPA[f][c] == 'S') {
        _fin = true; _gano = true;
        _puntosFinales = (uint16_t)((_finTiempo - now) / 1000UL);
        sound.play(Melody::AMOR);
    }
}

void Laberinto::render(U8G2& u8) {
    for (uint8_t f = 0; f < LAB_FILAS; f++) {
        for (uint8_t c = 0; c < LAB_COLS; c++) {
            char ch = MAPA[f][c];
            if (ch == '#') {
                u8.drawBox(c * LAB_CELDA, f * LAB_CELDA, LAB_CELDA, LAB_CELDA);
            } else if (ch == 'S') {
                // La llegada titila, para encontrarla de un vistazo
                if ((millis() / 300) % 2 == 0)
                    u8.drawFrame(c * LAB_CELDA, f * LAB_CELDA, LAB_CELDA, LAB_CELDA);
            }
        }
    }

    // Bolita en negativo: sobre el piso negro no se vería, pero al pasar
    // junto a una pared blanca el contorno la despega igual.
    u8.setDrawColor(1);
    u8.drawDisc((int)_x, (int)_y, 1);

    // Tiempo restante, arriba a la derecha sobre fondo limpio
    uint32_t restante = 0;
    int32_t d = (int32_t)(_finTiempo - millis());
    if (d > 0) restante = (uint32_t)d / 1000UL;
    char buf[8];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)restante);
    u8.setFont(u8g2_font_5x7_tf);
    int w = u8.getStrWidth(buf);
    u8.setDrawColor(0);
    u8.drawBox(126 - w, 0, w + 2, 9);
    u8.setDrawColor(1);
    u8.drawStr(127 - w, 7, buf);
}
