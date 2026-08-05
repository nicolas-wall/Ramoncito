#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "input.h"
#include "juego.h"

// ============================================================
//  arcade.h — Máquina de estados del mini arcade
//
//  Flujo:
//    MENU ──C──► JUGANDO ──(el juego avisa que terminó)──► FIN
//     ▲            │                                        │
//     │            └──A──► PAUSA ──A──► MENU                 │
//     └────────────────────C (revancha) / A (menú) ◄─────────┘
//
//  El botón A es siempre "atrás": desde el menú sale al idle (la cara),
//  desde el juego pausa, desde la pausa vuelve al menú. Nunca hay que
//  adivinar cómo salir.
//
//  El MENU es un carrusel horizontal: una tarjeta a pantalla completa por
//  juego, con su ícono y su nombre. La palanca (o el botón B) pasa de una a
//  otra con un deslizamiento, y la lista da la vuelta en los extremos.
//
//  El arcade no sabe nada de ningún juego en concreto: los maneja a través
//  de la interfaz Juego (ver juego.h). Todo lo compartido —el carrusel, la
//  pausa, la pantalla de fin, el récord en NVS— vive acá y no se repite.
// ============================================================

enum class ArcadeState : uint8_t { MENU, JUGANDO, PAUSA, FIN };

class Arcade {
public:
    void begin();

    // Entra al arcade, siempre por el menú de juegos.
    void enter(uint32_t now);

    // Navegación y acciones discretas.
    void handleEvent(InputEvent ev, uint32_t now);

    // Lógica del juego en curso. Llamar una vez por frame.
    void update(uint32_t now);

    void render(U8G2& u8);

    // true cuando el jugador salió: main vuelve a IDLE.
    bool quiereSalir() const { return _salir; }

    ArcadeState estado() const { return _estado; }

private:
    ArcadeState _estado  = ArcadeState::MENU;
    bool        _salir   = false;
    uint8_t     _sel     = 0;   // tarjeta visible del carrusel
    uint32_t    _timeout = 0;   // millis límite de inactividad

    Juego*   _juego = nullptr;  // juego en curso (nullptr fuera de partida)
    bool     _nuevoRecord = false;

    // Caché de récords, cargada de NVS una vez en begin(). El carrusel los
    // dibuja en cada frame: abrir y cerrar Preferences 30 veces por segundo
    // sería absurdo, y encima frenaría el deslizamiento.
    static const uint8_t MAX_JUEGOS = 12;
    uint16_t _records[MAX_JUEGOS] = {0};

    // --- Animación del carrusel ---
    // _slideDesde = 0 significa quieto. Durante el deslizamiento se dibujan
    // las DOS tarjetas, la que sale y la que entra, desplazadas en x.
    uint32_t _slideDesde = 0;
    int8_t   _slideDir   = 0;   // +1 = entra desde la derecha, -1 = izquierda
    uint8_t  _slidePrev  = 0;   // tarjeta que está saliendo

    void _mover(int8_t dir, uint32_t now);
    void _activar(uint32_t now);
    void _terminarPartida();

    void _cargarRecords();
    void _guardarRecord(uint8_t idx, uint16_t valor);

    void _renderMenu(U8G2& u8);
    void _renderTarjeta(U8G2& u8, uint8_t idx, int16_t dx);
    void _renderPausa(U8G2& u8);
    void _renderFin(U8G2& u8);
    void _tocarActividad(uint32_t now);
};

extern Arcade arcade;
