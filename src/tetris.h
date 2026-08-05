#pragma once
#include "juego.h"
#include "config.h"

// Tetris en un pozo de 10x15. Necesita la palanca: con tres botones no
// entran izquierda, derecha, rotar y bajar.
//   palanca ←/→  mover      palanca ↓  bajar rápido
//   botón B      rotar      botón C / pulsador  soltar de golpe
class Tetris : public Juego {
public:
    void begin(uint32_t now) override;
    void update(uint32_t now) override;
    void evento(InputEvent ev, uint32_t now) override;
    void render(U8G2& u8) override;
    bool terminado() const override { return _fin; }
    uint16_t puntaje() const override { return _puntos; }
    const char* resultado() const override { return "FIN"; }

private:
    // Una fila por bit: bit c = columna c ocupada. Buscar una línea
    // completa es comparar contra una máscara, sin recorrer celdas.
    uint16_t _pozo[TET_FILAS];

    uint8_t  _pieza, _rot;
    int8_t   _px, _py;          // esquina del cuadro 4x4 dentro del pozo
    uint8_t  _siguiente;
    uint16_t _puntos, _lineas;
    uint8_t  _nivel;
    bool     _fin;
    uint32_t _proximaCaida;

    bool _choca(uint8_t pieza, uint8_t rot, int8_t px, int8_t py) const;
    void _fijar();
    void _limpiarLineas();
    void _nuevaPieza();
    bool _bajar(uint32_t now);
};

extern Tetris juegoTetris;
