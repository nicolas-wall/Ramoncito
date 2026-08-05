#pragma once
#include "juego.h"

// Pong contra la CPU, a PONG_PUNTOS_GANAR puntos.
// Controles: eje Y de la palanca (proporcional) o botones B/C.
class Pong : public Juego {
public:
    void begin(uint32_t now) override;
    void update(uint32_t now) override;
    void render(U8G2& u8) override;
    bool terminado() const override { return _fin; }
    uint16_t puntaje() const override { return _ptsJug; }
    const char* resultado() const override { return _ganoJug ? "GANASTE" : "PERDISTE"; }
    // El puntaje tope es siempre 5: como récord no dice nada.
    bool tienePuntaje() const override { return false; }

private:
    float    _bolaX, _bolaY;   // esquina superior izquierda del cuadradito
    float    _velX,  _velY;
    float    _palJug;          // borde superior de cada paleta
    float    _palCpu;
    uint8_t  _ptsJug, _ptsCpu;
    uint32_t _saqueHasta;      // pausa antes del saque; 0 = en juego
    bool     _fin, _ganoJug;

    void _sacar(uint32_t now, bool haciaJug);
};

extern Pong juegoPong;
