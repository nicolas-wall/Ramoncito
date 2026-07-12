#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// =============================================================
//  face.h — Motor de cara y expresiones para espToy
//  Interfaz pública contra la que main.cpp está escrito.
// =============================================================

enum class Expression : uint8_t {
    NEUTRAL, FELIZ, TRISTE, ENOJADO, SORPRENDIDO,
    ABURRIDO, DORMIDO, SOSPECHOSO, AMOR
};

// Parámetros de un ojo (doc 03 §1.1).
// Declarado fuera de Face para que las funciones auxiliares de face.cpp
// puedan usarlo sin necesidad de acceso a miembros privados de la clase.
struct EyeParams {
    float cx, cy, w, h, r;
    float pTop, pBot;
    float slopeTop, slopeBot;
    float offX, offY;   // offset de mirada errante (compartido entre ambos ojos)
};

class Face {
public:
    void begin();                        // estado inicial: NEUTRAL, ojos abiertos
    void setExpression(Expression e);    // fija el objetivo de interpolación
    Expression expression() const;
    void update(uint32_t now);           // interpolación + parpadeo + mirada errante
    void render(U8G2 &u8);               // dibuja ambos ojos (no llama clearBuffer/sendBuffer)

private:
    // Estado de interpolación
    EyeParams _leftCur,  _rightCur;
    EyeParams _leftTgt,  _rightTgt;

    // Expresión activa
    Expression _expr;

    // Offset de mirada errante (compartido ambos ojos)
    float _gazeOffX, _gazeOffY;
    float _gazeTgtX, _gazeTgtY;
    uint32_t _gazeNextMs;

    // Micro-movimiento senoidal (respiración)
    float _breathPhase;   // ángulo en radianes, avanza cada frame

    // Estado de parpadeo
    enum class BlinkState : uint8_t { IDLE, CLOSING, CLOSED, OPENING };
    BlinkState _blinkState;
    uint8_t    _blinkFrame;
    uint32_t   _blinkNextMs;

    // helpers internos
    static void lerpEye(EyeParams &cur, const EyeParams &tgt, float t);
    void drawEye(U8G2 &u8, const EyeParams &p);
    void scheduleNextBlink(uint32_t now);
    void scheduleNextGaze(uint32_t now);
};

extern Face face;
