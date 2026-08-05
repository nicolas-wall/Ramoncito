#pragma once
#include <Arduino.h>
#include <U8g2lib.h>
#include "input.h"

// ============================================================
//  juego.h — Interfaz que implementa cada juego del arcade
//
//  El arcade no sabe nada de ningún juego en particular: los arranca,
//  los actualiza, los dibuja y les pregunta si terminaron. Todo lo que
//  es común —el carrusel, la pantalla de fin de partida, el récord, la
//  pausa— vive en arcade.cpp y no se repite en cada juego.
//
//  Un juego lee los controles por su cuenta desde `input`, porque cada
//  uno los usa distinto: Pong quiere el eje analógico continuo, Snake
//  quiere pulsos discretos, el laberinto quiere el IMU.
//
//  Contrato de tiempo: update() se llama una vez por frame (~30 fps) con
//  el mismo `now` que usa todo el firmware. Los juegos integran a paso
//  fijo por frame, sin escalar por delta: el loop mantiene los 30 fps, y
//  a estas velocidades el paso fijo es más predecible —un frame largo no
//  puede teletransportar una pelota a través de una paleta—.
// ============================================================

class Juego {
public:
    virtual ~Juego() {}

    // Prepara una partida nueva. Se llama también en cada revancha.
    virtual void begin(uint32_t now) = 0;

    // Lógica y física de un frame.
    virtual void update(uint32_t now) = 0;

    // Evento discreto de control durante la partida. El arcade le pasa
    // todo menos el botón A, que se queda para la pausa.
    //
    // Existe además de update() porque hay dos clases de control que no se
    // pueden servir igual: una paleta quiere el estado sostenido del eje,
    // pero mover una pieza de Tetris una celda por pulsación quiere el
    // evento —con el auto-repeat que input ya calcula—. Leer el estado
    // sostenido para eso obligaría a cada juego a reimplementar su propio
    // anti-rebote y su propia repetición.
    virtual void evento(InputEvent ev, uint32_t now) { (void)ev; (void)now; }

    // Dibuja en el buffer. No llama clearBuffer() ni sendBuffer().
    virtual void render(U8G2& u8) = 0;

    // true cuando la partida terminó: el arcade pasa a la pantalla de fin.
    virtual bool terminado() const = 0;

    // Puntaje final, para mostrarlo y compararlo contra el récord.
    virtual uint16_t puntaje() const = 0;

    // Título de la pantalla de fin: "GANASTE", "PERDISTE", "FIN"...
    virtual const char* resultado() const { return "FIN"; }

    // true si el puntaje de este juego sirve como récord. Los juegos donde
    // se gana o se pierde sin acumular nada devuelven false, y entonces el
    // arcade no muestra ni guarda récord.
    virtual bool tienePuntaje() const { return true; }
};
