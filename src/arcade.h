#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "input.h"

// ============================================================
//  arcade.h — Máquina de estados del mini arcade
//
//  Flujo:
//    MENU ──C──► JUGANDO ──(alguien llega a PONG_PUNTOS_GANAR)──► FIN
//     ▲            │                                              │
//     │            └──A──► PAUSA ──A──► MENU                       │
//     └──────────────────────────C (revancha) / A (menú) ◄─────────┘
//
//  El botón A es siempre "atrás": desde el menú sale al idle (la cara),
//  desde el juego pausa, desde la pausa vuelve al menú. Nunca hay que
//  adivinar cómo salir.
//
//  Los controles se leen de dos formas según haga falta:
//    - Eventos (handleEvent) para navegar menús: un paso por pulsación.
//    - Estado continuo (input.axisY / btnB / btnC dentro de update) para
//      la paleta, que necesita movimiento sostenido y proporcional.
// ============================================================

enum class ArcadeState : uint8_t { MENU, JUGANDO, PAUSA, FIN };

class Arcade {
public:
    void begin();

    // Entra al arcade, siempre por el menú de juegos.
    void enter(uint32_t now);

    // Navegación y acciones discretas.
    void handleEvent(InputEvent ev, uint32_t now);

    // Física y lógica del juego. Llamar una vez por frame.
    void update(uint32_t now);

    void render(U8G2& u8);

    // true cuando el jugador salió: main vuelve a IDLE.
    bool quiereSalir() const { return _salir; }

    ArcadeState estado() const { return _estado; }

private:
    ArcadeState _estado   = ArcadeState::MENU;
    bool        _salir    = false;
    uint8_t     _sel      = 0;   // ítem resaltado del menú
    uint32_t    _timeout  = 0;   // millis límite de inactividad

    // --- Estado de la partida de Pong ---
    struct Pong {
        float    bolaX, bolaY;     // esquina superior izquierda del cuadradito
        float    velX,  velY;
        float    palJug;           // borde superior de cada paleta
        float    palCpu;
        uint8_t  ptsJug, ptsCpu;
        uint32_t saqueHasta;       // pausa antes del saque; 0 = en juego
        bool     saqueHaciaJug;    // a quién va dirigido el próximo saque
        bool     ganoJug;          // válido en FIN
    };
    Pong _p;

    void _resetPartida(uint32_t now);
    void _sacar(uint32_t now, bool haciaJug);
    void _updatePong(uint32_t now);
    void _renderMenu(U8G2& u8);
    void _renderPong(U8G2& u8);
    void _renderFin(U8G2& u8);
    void _renderPausa(U8G2& u8);
    void _tocarActividad(uint32_t now);
};

extern Arcade arcade;
