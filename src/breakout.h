#pragma once
#include "juego.h"
#include "config.h"

// Breakout: paleta abajo, ladrillos arriba, tres vidas.
// Reusa la misma física de pelota que Pong (rebote con efecto según dónde
// pega en la paleta), pero la colisión con ladrillos va por celda de
// grilla y no por caja: a 3 px por frame la pelota se colaría entre dos.
class Breakout : public Juego {
public:
    void begin(uint32_t now) override;
    void update(uint32_t now) override;
    void render(U8G2& u8) override;
    bool terminado() const override { return _fin; }
    uint16_t puntaje() const override { return _puntos; }
    const char* resultado() const override { return _gano ? "GANASTE" : "PERDISTE"; }

private:
    // Bitmap de ladrillos vivos: una fila por bit-set de columnas.
    bool     _ladrillo[BRK_FILAS][BRK_COLS];
    uint8_t  _quedan;

    float    _bolaX, _bolaY, _velX, _velY;
    float    _paleta;          // borde izquierdo de la paleta
    uint8_t  _vidas;
    uint16_t _puntos;
    bool     _fin, _gano;
    uint32_t _saqueHasta;      // pelota pegada a la paleta antes del saque

    void _sacar(uint32_t now);
    void _rebotarLadrillo(int8_t col, int8_t fila);
};

extern Breakout juegoBreakout;
