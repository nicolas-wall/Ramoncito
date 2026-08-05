#pragma once
#include "juego.h"
#include "config.h"

// Laberinto por inclinación: se inclina el gabinete y la bolita rueda.
// Usa el acelerómetro, que hasta ahora solo servía para poner caras.
// Si el IMU no responde, cae de vuelta en la palanca para que el juego
// siga siendo jugable.
class Laberinto : public Juego {
public:
    void begin(uint32_t now) override;
    void update(uint32_t now) override;
    void render(U8G2& u8) override;
    bool terminado() const override { return _fin; }
    // Puntaje = segundos que sobraron. Así "más es mejor", como en el resto,
    // y llegar rápido puntúa más que llegar justo.
    uint16_t puntaje() const override { return _puntosFinales; }
    const char* resultado() const override { return _gano ? "LLEGASTE" : "SE ACABO"; }

private:
    float    _x, _y;       // centro de la bolita, en píxeles
    float    _vx, _vy;
    // Cero de inclinación tomado al empezar: el baseline del IMU se adapta
    // solo y haría desvanecer una inclinación sostenida (ver imu.h).
    float    _cero1, _cero2;
    uint32_t _finTiempo;
    bool     _fin, _gano;
    uint16_t _puntosFinales;

    bool  _pared(float px, float py) const;
    void  _leerInclinacion(float& ax, float& ay);
};

extern Laberinto juegoLaberinto;
