#pragma once
#include <Arduino.h>

// ============================================================
//  input.h — Módulo de entradas: botones y touch capacitivo
//  Seeed XIAO ESP32-S3, framework Arduino
// ============================================================

enum class InputEvent : uint8_t {
    NONE,
    BTN_A_PRESS,
    BTN_B_PRESS,
    TOUCH_START,
    TOUCH_END,
    COMBO_AB_3S   // ambos botones sostenidos >= 3 s (minijuego oculto, doc 04 §1)
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
    bool     btnA()          const;
    bool     btnB()          const;
    bool     touching()      const;
    uint32_t touchValue()    const;  // última lectura cruda del touch
    uint32_t touchBaseline() const;  // baseline de calibración

private:
    // --- Cola FIFO de 8 slots (array circular, sin malloc) ---
    static const uint8_t QUEUE_SIZE = 8;
    InputEvent _queue[QUEUE_SIZE];
    uint8_t    _qHead;  // índice del próximo evento a consumir
    uint8_t    _qTail;  // índice donde escribir el próximo evento
    uint8_t    _qCount; // cantidad de eventos en la cola

    void _enqueue(InputEvent ev);

    // --- Estado de botones ---
    // Para cada botón: estado actual debounced, estado "raw" leído,
    // y timestamp del último cambio raw.
    struct BtnState {
        bool     debounced;     // estado aceptado (true = presionado)
        bool     raw;           // último valor leído del pin
        uint32_t lastChangeMs;  // cuándo cambió "raw" por última vez
    };
    BtnState _btnA;
    BtnState _btnB;

    void _pollBtn(BtnState& btn, uint8_t pin, InputEvent evPress,
                  const char* label, uint32_t now);

    // --- Combo secreto A+B sostenido (minijuego oculto) ---
    uint32_t _comboStartMs;   // cuándo el segundo botón se sumó (0 = no corriendo)
    bool     _comboEmitted;   // ya se emitió; esperar a soltar ambos para rearmar

    void _pollCombo(uint32_t now);

    // --- Estado del touch capacitivo ---
    uint32_t _touchBaseline;    // línea base calculada en begin()
    uint32_t _touchThreshHigh;  // umbral de "entrada al toque"
    uint32_t _touchThreshLow;   // umbral de "salida del toque" (histéresis)
    uint32_t _touchLastValue;   // última lectura cruda
    uint32_t _touchLastPollMs;  // cuándo se hizo la última lectura
    bool     _isTouching;       // estado actual confirmado
    uint8_t  _touchConsecHigh;  // lecturas consecutivas sobre umbral alto
    uint8_t  _touchConsecLow;   // lecturas consecutivas bajo umbral bajo

    void _pollTouch(uint32_t now);
};

extern Input input;
