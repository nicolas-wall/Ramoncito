#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// =============================================================
//  face.h — Motor de cara y expresiones para espToy
//  Interfaz pública contra la que main.cpp está escrito.
//
//  v3 (doc 06 §2): cada expresión es una animación con fases
//  INTRO → LOOP → OUTRO, más un sistema de partículas para los
//  extras (corazones, Zzz, lágrima).
// =============================================================

enum class Expression : uint8_t {
    NEUTRAL, FELIZ, TRISTE, ENOJADO, SORPRENDIDO,
    ABURRIDO, DORMIDO, SOSPECHOSO, AMOR, GUINO,
    RISA,      // risa por cosquillas ("^^" enérgico)
    MAREADO,   // ojos en espiral giratoria (@_@) — efecto mareo
    ILUSIONADO // ojos grandes brillantes mirando arriba (cuando lo alzan)
};

// Gesto idle que puede estar activo mientras la cara está en LOOP
enum class GestoIdle : uint8_t {
    NINGUNO,      // sin gesto activo
    BOSTEZO,      // válido en NEUTRAL y ABURRIDO
    SACUDIDA,     // solo en NEUTRAL
    MIRADA_FIJA   // solo en NEUTRAL
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

// Partícula de los extras animados (doc 06 §2.1)
struct Particula {
    uint8_t  tipo;      // 0 = libre; ver PART_* en face.cpp
    float    x, y;
    float    vx, vy;
    uint32_t nacioMs;
    uint16_t vidaMs;
};

class Face {
public:
    void begin();                        // estado inicial: NEUTRAL, ojos abiertos
    void setExpression(Expression e);    // dispara OUTRO → INTRO hacia la nueva expresión
    Expression expression() const;
    void update(uint32_t now);           // fases + loop por expresión + partículas
    void render(U8G2 &u8);               // dibuja ojos + extras (no llama clearBuffer/sendBuffer)

private:
    // Fases de animación (doc 06 §2.1)
    enum class AnimFase : uint8_t { INTRO, LOOP, OUTRO };
    AnimFase   _fase;
    uint32_t   _faseInicioMs;
    Expression _pendiente;      // expresión destino durante el OUTRO
    bool       _hayPendiente;

    // Moduladores del loop (se aplican al dibujar, no mutan _cur)
    float _animOffX, _animOffY;   // desplazamiento extra de ambos ojos
    float _animEscala;            // escala uniforme (overshoot de INTRO, pulso AMOR)
    float _animEscalaY;           // escala vertical (squash del OUTRO)
    float _lidExtra;              // párpado extra (ABURRIDO: cierre lento)
    float _loopPhase;             // fase genérica del loop activo
    uint32_t _sigTemblorMs;       // próximo cambio del temblor ENOJADO
    float    _temblorX;

    // Partículas
    Particula _partes[4];
    uint32_t  _sigSpawnMs;
    bool      _spawnLado;         // alterna el lado de nacimiento (corazones)

    uint32_t _lastNow;            // millis del último update (para extras en render)

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

    // Gestos idle (doc 03 §3.4)
    GestoIdle _gesto;           // gesto actualmente en curso
    uint32_t  _gestoInicioMs;   // millis() cuando empezó el gesto activo

    // Timers de próximo disparo para cada gesto
    uint32_t  _sigBostezo;
    uint32_t  _sigSacudida;
    uint32_t  _sigMiradaFija;

    // helpers internos
    static void lerpEye(EyeParams &cur, const EyeParams &tgt, float t);
    void drawEye(U8G2 &u8, const EyeParams &p);
    void scheduleNextBlink(uint32_t now);
    void scheduleNextGaze(uint32_t now);
    void scheduleNextBostezo(uint32_t now);
    void scheduleNextSacudida(uint32_t now);
    void scheduleNextMiradaFija(uint32_t now);
    void cambiarExpresion(Expression e, uint32_t now);  // carga targets + INTRO
    void updateLoop(uint32_t now);                       // moduladores por expresión
    void updateGestos(uint32_t now);                     // gestos idle (se llama tras updateLoop)
    void spawnParticula(uint8_t tipo, float x, float y,
                        float vx, float vy, uint16_t vidaMs, uint32_t now);
    void updateParticulas(uint32_t now);
    void limpiarParticulas();
    void renderParticulas(U8G2 &u8);
    void renderExtras(U8G2 &u8);                         // furia, "...", "?"
};

extern Face face;
