#pragma once
#include <Arduino.h>

// ============================================================
//  input.h — Módulo de entradas: botones y touch capacitivo
//  Seeed XIAO ESP32-S3, framework Arduino
// ============================================================

enum class InputEvent : uint8_t {
    NONE,
    BTN_A_PRESS,
    TOUCH_START,
    TOUCH_END,
    TICKLE_START   // toque en el sensor del pie
};

class Input {
public:
    // Configura pines y autocalibra el touch (~1 s). Llamar en setup().
    void begin();

    // Llamar en cada pasada del loop(). Actualiza estado y encola eventos.
    void poll(uint32_t now);

    // Devuelve el próximo evento de la cola FIFO; NONE si está vacía.
    InputEvent nextEvent();

    // Estado crudo para debug/pantalla
    bool     btnA()             const;
    bool     touching()         const;
    bool     touchingPie()      const;
    uint32_t touchValue()       const;
    uint32_t touchBaseline()    const;
    uint32_t touchValuePie()    const;
    uint32_t touchBaselinePie() const;

private:
    // --- Cola FIFO de 8 slots (array circular, sin malloc) ---
    static const uint8_t QUEUE_SIZE = 8;
    InputEvent _queue[QUEUE_SIZE];
    uint8_t    _qHead;  // índice del próximo evento a consumir
    uint8_t    _qTail;  // índice donde escribir el próximo evento
    uint8_t    _qCount; // cantidad de eventos en la cola

    void _enqueue(InputEvent ev);

    // --- Estado del botón ---
    struct BtnState {
        bool     debounced;
        bool     raw;
        uint32_t lastChangeMs;
    };
    BtnState _btnA;

    void _pollBtn(BtnState& btn, uint8_t pin, InputEvent evPress,
                  const char* label, uint32_t now);

    // --- Estado de los sensores táctiles ---
    struct TouchState {
        uint32_t baseline;
        uint32_t threshHigh;
        uint32_t threshLow;
        uint32_t lastValue;
        uint32_t lastPollMs;
        bool     isTouching;
        uint8_t  consecHigh;
        uint8_t  consecLow;
    };
    TouchState _touch;     // caricia (cabeza)
    TouchState _touchPie;  // cosquillas (pie)

    void _calibrateTouch(TouchState& t, uint8_t pin, const char* label);
    void _pollTouch(TouchState& t, uint8_t pin,
                    InputEvent evStart, InputEvent evEnd,
                    bool emitEnd, uint32_t now);
};

extern Input input;
