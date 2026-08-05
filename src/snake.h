#pragma once
#include "juego.h"
#include "config.h"

// Snake clásico sobre una grilla de SNAKE_CELDA px.
// Controles: palanca en 4 direcciones, o B/C que giran a izquierda y
// derecha en relativo — así se puede jugar entero con dos botones.
class Snake : public Juego {
public:
    void begin(uint32_t now) override;
    void update(uint32_t now) override;
    void evento(InputEvent ev, uint32_t now) override;
    void render(U8G2& u8) override;
    bool terminado() const override { return _fin; }
    uint16_t puntaje() const override { return _puntos; }
    const char* resultado() const override { return "FIN"; }

private:
    // El cuerpo es un buffer circular de celdas: crecer es solo no borrar
    // la cola en ese frame, sin mover nada de memoria.
    static const uint16_t MAX_LARGO = 220;
    uint8_t  _cx[MAX_LARGO], _cy[MAX_LARGO];
    uint16_t _cabeza;   // índice de la cabeza dentro del buffer
    uint16_t _largo;

    int8_t   _dx, _dy;      // dirección actual
    int8_t   _pdx, _pdy;    // dirección ya aplicada este paso (anti-giro doble)
    uint8_t  _comidaX, _comidaY;
    uint16_t _puntos;
    bool     _fin;
    uint32_t _proximoPaso;

    void _nuevaComida();
    bool _ocupada(uint8_t x, uint8_t y) const;
    void _leerControles();
};

extern Snake juegoSnake;
