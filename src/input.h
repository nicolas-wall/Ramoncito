#pragma once
#include <Arduino.h>

// ============================================================
//  input.h — Entradas del arcade: botones + palanca analógica
//  Seeed XIAO ESP32-S3, framework Arduino
//
//  Dos formas de leer la palanca, según lo que necesite quien llama:
//    - Continua  → axisX()/axisY(), float -1..+1 ya con zona muerta.
//                  Es lo que usan los juegos (paleta proporcional).
//    - Discreta  → eventos JOY_* con histéresis y auto-repeat.
//                  Es lo que usan los menús (un paso por golpe).
//
//  Mientras JOYSTICK_PRESENTE sea false los ejes se alimentan del stub
//  (setStubAxis desde comandos seriales) en vez del ADC, así todo el
//  firmware se puede probar antes de tener la palanca en la mano.
// ============================================================

enum class InputEvent : uint8_t {
    NONE,
    BTN_A_PRESS,    // botón 1
    BTN_B_PRESS,    // botón 2
    BTN_C_PRESS,    // botón 3 / START
    JOY_SW_PRESS,   // pulsador de la palanca
    JOY_UP,
    JOY_DOWN,
    JOY_LEFT,
    JOY_RIGHT
};

class Input {
public:
    // Configura pines y calibra el centro de la palanca. Llamar en setup().
    void begin();

    // Llamar en cada pasada del loop(). Actualiza estado y encola eventos.
    void poll(uint32_t now);

    // Devuelve el próximo evento de la cola FIFO; NONE si está vacía.
    InputEvent nextEvent();

    // --- Estado de botones (ya con debounce) ---
    bool btnA()   const;
    bool btnB()   const;
    bool btnC()   const;
    bool joySw()  const;
    bool anyBtn() const;   // cualquiera de los cuatro

    // --- Estado continuo de la palanca ---
    float axisX() const;   // -1 izquierda .. +1 derecha
    float axisY() const;   // -1 arriba     .. +1 abajo

    // --- Estado discreto de la palanca (con histéresis) ---
    bool left()  const;
    bool right() const;
    bool up()    const;
    bool down()  const;

    // --- Diagnóstico (log serial / panel web) ---
    bool     joystickPresente() const;
    uint16_t rawX()    const;
    uint16_t rawY()    const;
    uint16_t centroX() const;
    uint16_t centroY() const;

    // Stub sin hardware: fija los ejes a mano (-1..+1). Vuelven solos al
    // centro tras JOY_STUB_AUTOCENTRO_MS. No hace nada si hay palanca real.
    void setStubAxis(float x, float y, uint32_t now);

    // Encola un evento como si viniera del hardware. Pensado para probar
    // por serial los botones que todavía no están cableados.
    void injectEvent(InputEvent ev);

private:
    // --- Cola FIFO de 8 slots (array circular, sin malloc) ---
    static const uint8_t QUEUE_SIZE = 8;
    InputEvent _queue[QUEUE_SIZE];
    uint8_t    _qHead;   // índice del próximo evento a consumir
    uint8_t    _qTail;   // índice donde escribir el próximo evento
    uint8_t    _qCount;  // cantidad de eventos en la cola

    void _enqueue(InputEvent ev);

    // --- Botones ---
    struct BtnState {
        bool     debounced;
        bool     raw;
        uint32_t lastChangeMs;
    };
    BtnState _btnA, _btnB, _btnC, _joySw;

    void _pollBtn(BtnState& btn, uint8_t pin, InputEvent evPress,
                  const char* label, uint32_t now);

    // --- Ejes de la palanca ---
    // dir vale -1, 0 o +1 y solo cambia por histéresis (UMBRAL_ON/OFF),
    // nunca directamente por el valor crudo.
    struct AxisState {
        uint16_t centro;        // lectura ADC en reposo (calibrada al boot)
        uint16_t raw;           // última lectura cruda
        float    value;         // normalizado -1..+1 con zona muerta aplicada
        int8_t   dir;           // -1, 0, +1 tras histéresis
        uint32_t nextRepeatMs;  // próximo auto-repeat mientras se sostiene
    };
    AxisState _ejeX, _ejeY;

    uint32_t _lastJoyPollMs;

    // Stub (sin palanca física)
    float    _stubX, _stubY;
    uint32_t _stubHasta;

    uint16_t _calibrarEje(uint8_t pin, const char* label);
    float    _normalizar(const AxisState& eje, uint16_t raw, bool invertir) const;
    void     _pollEje(AxisState& eje, float valor,
                      InputEvent evNeg, InputEvent evPos, uint32_t now);
};

extern Input input;
