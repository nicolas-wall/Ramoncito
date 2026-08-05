// ============================================================
//  tetris.cpp — Tetris
//
//  Las piezas se guardan como 4 filas de 4 bits, con el BIT 3 a la
//  izquierda. Eso permite escribirlas en binario y que se lean como el
//  dibujo: 0b0110 es ".##.". Con bit 0 a la izquierda habría que leerlas
//  al revés, que es exactamente el tipo de detalle que produce piezas
//  espejadas sin que nadie entienda por qué.
// ============================================================
#include "tetris.h"
#include "config.h"
#include "sound.h"

Tetris juegoTetris;

struct Pieza { uint8_t rot[4][4]; };

static const Pieza PIEZAS[7] = {
    // I
    {{{0b0000, 0b1111, 0b0000, 0b0000},
      {0b0010, 0b0010, 0b0010, 0b0010},
      {0b0000, 0b1111, 0b0000, 0b0000},
      {0b0010, 0b0010, 0b0010, 0b0010}}},
    // O
    {{{0b0110, 0b0110, 0b0000, 0b0000},
      {0b0110, 0b0110, 0b0000, 0b0000},
      {0b0110, 0b0110, 0b0000, 0b0000},
      {0b0110, 0b0110, 0b0000, 0b0000}}},
    // T
    {{{0b0100, 0b1110, 0b0000, 0b0000},
      {0b0100, 0b0110, 0b0100, 0b0000},
      {0b0000, 0b1110, 0b0100, 0b0000},
      {0b0100, 0b1100, 0b0100, 0b0000}}},
    // S
    {{{0b0110, 0b1100, 0b0000, 0b0000},
      {0b0100, 0b0110, 0b0010, 0b0000},
      {0b0110, 0b1100, 0b0000, 0b0000},
      {0b0100, 0b0110, 0b0010, 0b0000}}},
    // Z
    {{{0b1100, 0b0110, 0b0000, 0b0000},
      {0b0010, 0b0110, 0b0100, 0b0000},
      {0b1100, 0b0110, 0b0000, 0b0000},
      {0b0010, 0b0110, 0b0100, 0b0000}}},
    // J
    {{{0b1000, 0b1110, 0b0000, 0b0000},
      {0b0110, 0b0100, 0b0100, 0b0000},
      {0b0000, 0b1110, 0b0010, 0b0000},
      {0b0100, 0b0100, 0b1100, 0b0000}}},
    // L
    {{{0b0010, 0b1110, 0b0000, 0b0000},
      {0b0100, 0b0100, 0b0110, 0b0000},
      {0b0000, 0b1110, 0b1000, 0b0000},
      {0b1100, 0b0100, 0b0100, 0b0000}}},
};

static inline bool celda(uint8_t pieza, uint8_t rot, uint8_t x, uint8_t y) {
    return (PIEZAS[pieza].rot[rot][y] >> (3 - x)) & 1;
}

static const uint16_t FILA_LLENA = (1u << TET_COLS) - 1;

// ============================================================

void Tetris::begin(uint32_t now) {
    for (uint8_t f = 0; f < TET_FILAS; f++) _pozo[f] = 0;
    _puntos = 0; _lineas = 0; _nivel = 1;
    _fin = false;
    _siguiente = (uint8_t)(esp_random() % 7);
    _proximaCaida = now + TET_CAIDA_MS;
    _nuevaPieza();
}

bool Tetris::_choca(uint8_t pieza, uint8_t rot, int8_t px, int8_t py) const {
    for (uint8_t y = 0; y < 4; y++) {
        for (uint8_t x = 0; x < 4; x++) {
            if (!celda(pieza, rot, x, y)) continue;
            int8_t cx = px + x, cy = py + y;
            if (cx < 0 || cx >= TET_COLS) return true;
            if (cy >= TET_FILAS) return true;
            // Arriba del pozo se permite: las piezas entran desde afuera.
            if (cy < 0) continue;
            if (_pozo[cy] & (1u << cx)) return true;
        }
    }
    return false;
}

void Tetris::_nuevaPieza() {
    _pieza = _siguiente;
    _siguiente = (uint8_t)(esp_random() % 7);
    _rot = 0;
    _px = TET_COLS / 2 - 2;
    _py = -1;
    // Si la pieza nueva ya no entra, se acabó.
    if (_choca(_pieza, _rot, _px, _py)) {
        _fin = true;
        sound.play(Melody::TRISTE);
    }
}

void Tetris::_fijar() {
    for (uint8_t y = 0; y < 4; y++)
        for (uint8_t x = 0; x < 4; x++)
            if (celda(_pieza, _rot, x, y)) {
                int8_t cy = _py + y, cx = _px + x;
                if (cy >= 0 && cy < TET_FILAS) _pozo[cy] |= (1u << cx);
            }
}

void Tetris::_limpiarLineas() {
    uint8_t hechas = 0;
    for (int8_t f = TET_FILAS - 1; f >= 0; f--) {
        if (_pozo[f] != FILA_LLENA) continue;
        hechas++;
        // Bajar todo lo que estaba encima una fila.
        for (int8_t k = f; k > 0; k--) _pozo[k] = _pozo[k - 1];
        _pozo[0] = 0;
        f++;   // revisar de nuevo esta misma fila, que ahora tiene otra
    }
    if (!hechas) return;

    _lineas += hechas;
    // Progresión clásica: varias líneas de una valen mucho más que las
    // mismas líneas de a una. Es lo que empuja a arriesgarse a apilar.
    static const uint16_t VALOR[5] = { 0, 10, 30, 70, 150 };
    _puntos += VALOR[hechas > 4 ? 4 : hechas];
    _nivel = 1 + _lineas / TET_LINEAS_NIVEL;
    sound.play(hechas >= 4 ? Melody::AMOR : Melody::FELIZ);
}

bool Tetris::_bajar(uint32_t now) {
    if (!_choca(_pieza, _rot, _px, _py + 1)) { _py++; return true; }
    _fijar();
    _limpiarLineas();
    _nuevaPieza();
    _proximaCaida = now + TET_CAIDA_MS;
    return false;
}

void Tetris::evento(InputEvent ev, uint32_t now) {
    if (_fin) return;

    if (ev == InputEvent::JOY_LEFT) {
        if (!_choca(_pieza, _rot, _px - 1, _py)) _px--;
    } else if (ev == InputEvent::JOY_RIGHT) {
        if (!_choca(_pieza, _rot, _px + 1, _py)) _px++;
    } else if (ev == InputEvent::JOY_DOWN) {
        _bajar(now);
    } else if (ev == InputEvent::BTN_B_PRESS) {
        uint8_t nr = (uint8_t)((_rot + 1) % 4);
        // Wall kick mínimo: si la rotación no entra donde está, se prueba
        // corrida uno o dos lugares. Sin esto, rotar pegado a una pared es
        // imposible y el juego se siente trabado.
        const int8_t kicks[5] = { 0, -1, 1, -2, 2 };
        for (uint8_t k = 0; k < 5; k++) {
            if (!_choca(_pieza, nr, _px + kicks[k], _py)) {
                _rot = nr;
                _px += kicks[k];
                sound.play(Melody::BIP);
                break;
            }
        }
    } else if (ev == InputEvent::BTN_C_PRESS || ev == InputEvent::JOY_SW_PRESS) {
        while (_bajar(now)) { }   // soltar de golpe
        sound.play(Melody::BIP);
    }
}

void Tetris::update(uint32_t now) {
    if (_fin) return;
    if ((int32_t)(now - _proximaCaida) < 0) return;

    uint32_t caida = TET_CAIDA_MS - (uint32_t)(_nivel - 1) * 60;
    if (caida < TET_CAIDA_MIN_MS) caida = TET_CAIDA_MIN_MS;
    _proximaCaida = now + caida;
    _bajar(now);
}

void Tetris::render(U8G2& u8) {
    // Pozo
    u8.drawFrame(TET_X0 - 1, TET_Y0 - 1, TET_COLS * TET_CELDA + 2, TET_FILAS * TET_CELDA + 2);

    for (uint8_t f = 0; f < TET_FILAS; f++)
        for (uint8_t c = 0; c < TET_COLS; c++)
            if (_pozo[f] & (1u << c))
                u8.drawBox(TET_X0 + c * TET_CELDA, TET_Y0 + f * TET_CELDA,
                           TET_CELDA - 1, TET_CELDA - 1);

    // Pieza en juego
    for (uint8_t y = 0; y < 4; y++)
        for (uint8_t x = 0; x < 4; x++)
            if (celda(_pieza, _rot, x, y)) {
                int8_t cy = _py + y;
                if (cy < 0) continue;   // la parte que todavía está afuera
                u8.drawBox(TET_X0 + (_px + x) * TET_CELDA, TET_Y0 + cy * TET_CELDA,
                           TET_CELDA - 1, TET_CELDA - 1);
            }

    // Panel derecho
    char buf[16];
    const int px = TET_X0 + TET_COLS * TET_CELDA + 8;
    u8.setFont(u8g2_font_5x7_tf);
    u8.drawStr(px, 9, "PUNTOS");
    snprintf(buf, sizeof(buf), "%u", _puntos);
    u8.drawStr(px, 19, buf);
    u8.drawStr(px, 31, "NIVEL");
    snprintf(buf, sizeof(buf), "%u", _nivel);
    u8.drawStr(px, 41, buf);

    u8.drawStr(px, 53, "SIG");
    for (uint8_t y = 0; y < 4; y++)
        for (uint8_t x = 0; x < 4; x++)
            if (celda(_siguiente, 0, x, y))
                u8.drawBox(px + 26 + x * 3, 48 + y * 3, 2, 2);
}
