#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// Resultado de la partida
enum class PongResult : uint8_t { EN_JUEGO, GANA_JUGADOR, GANA_CPU };

class Pong {
public:
    // Resetea la partida y suena el jingle de inicio
    void enter();

    // Limpia estado
    void exit();

    // Llamar una vez por frame (~30 fps).
    // btnArriba/btnAbajo = estado crudo (mantenido) de los botones
    // para mover la paleta del jugador.
    void update(uint32_t now, bool btnArriba, bool btnAbajo);

    // Dibuja el campo — NO llama clearBuffer/sendBuffer
    void render(U8G2 &u8);

    // EN_JUEGO mientras se juega, GANA_JUGADOR / GANA_CPU al terminar
    PongResult result() const;

    // true cuando terminó (incluida la pantalla de resultado de 2 s)
    bool done() const;

private:
    // ---- Estado general ----
    bool _activo;          // el módulo está en uso
    bool _terminado;       // la pantalla de resultado ya expiró
    PongResult _resultado; // resultado actual

    // ---- Pelota ----
    float _bx, _by;    // posición del centro
    float _bvx, _bvy;  // velocidad en px/frame

    // ---- Paleta jugador (izquierda) ----
    float _pyY;  // y del borde superior

    // ---- Paleta CPU (derecha) ----
    float _cpuY; // y del borde superior

    // ---- Marcador ----
    uint8_t _puntosJugador;
    uint8_t _puntosCPU;

    // ---- IA ----
    float    _cpuOffset;         // error actual aplicado al target
    float    _cpuOffsetTarget;   // nuevo valor de error (se interpola)
    int16_t  _cpuOffsetTimer;    // frames restantes hasta el próximo cambio
    int16_t  _cpuOffsetInterval; // intervalo sorteado (40-80 frames)

    // ---- Protección anti-doble-rebote ----
    bool _colisionadoIzq; // ya chocó con la paleta del jugador este tiro
    bool _colisionadoDer; // ya chocó con la paleta CPU este tiro

    // ---- Pantalla de resultado ----
    uint32_t _resultHasta; // millis() hasta que termina la pausa de 2 s

    // ---- Métodos internos ----
    void _resetBola(bool haciaJugador); // saca la pelota al centro
    void _actualizarIA();               // mueve la paleta CPU
    void _getDificultad(float &baseSpeed, float &errorRange) const;
};

// Instancia global
extern Pong pong;
