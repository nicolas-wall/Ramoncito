#pragma once
#include "juego.h"
#include "config.h"

// Invaders: una formación que baja, un cañón abajo, tres vidas.
// Controles: eje X de la palanca o B/C para moverse; el pulsador de la
// palanca dispara. Sin palanca no queda ningún botón libre —B y C están
// moviendo la nave—, así que el cañón dispara solo, en cadencia fija: se
// juega esquivando y apuntando con la posición.
class Invaders : public Juego {
public:
    void begin(uint32_t now) override;
    void update(uint32_t now) override;
    void render(U8G2& u8) override;
    bool terminado() const override { return _fin; }
    uint16_t puntaje() const override { return _puntos; }
    const char* resultado() const override { return _gano ? "GANASTE" : "PERDISTE"; }

private:
    bool     _vivo[INV_FILAS][INV_COLS];
    uint8_t  _quedan;
    float    _formX;          // desplazamiento horizontal de la formación
    float    _formY;          // desplazamiento vertical acumulado
    int8_t   _formDir;
    uint32_t _proximoPaso;    // la formación avanza a saltos, no continuo

    float    _naveX;
    bool     _tiroActivo;  float _tiroX, _tiroY;
    bool     _bombaActiva; float _bombaX, _bombaY;
    uint32_t _proximaBomba;
    uint32_t _proximoAuto;   // cadencia del disparo automático

    uint8_t  _vidas;
    uint16_t _puntos;
    bool     _fin, _gano;

    void _disparar();
    void _leerControles(uint32_t now);
    bool _colisionTiro();
};

extern Invaders juegoInvaders;
