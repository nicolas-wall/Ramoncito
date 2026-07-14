// ============================================================
//  input.cpp — Módulo de entradas: botón y touch capacitivo
//  Seeed XIAO ESP32-S3, framework Arduino
//
//  IMPORTANTE — comportamiento del touch en ESP32-S3:
//    touchRead() devuelve valores GRANDES en reposo (20000-30000)
//    y AUMENTAN al tocar. Contrario al ESP32 original.
//    Umbral: valor > baseline * TOUCH_FACTOR_UMBRAL  →  toque detectado.
// ============================================================
#include "input.h"
#include "config.h"

Input input;

// ============================================================
//  Cola FIFO
// ============================================================

void Input::_enqueue(InputEvent ev) {
    if (_qCount >= QUEUE_SIZE) {
        _qHead = (_qHead + 1) % QUEUE_SIZE;
        _qCount--;
    }
    _queue[_qTail] = ev;
    _qTail  = (_qTail + 1) % QUEUE_SIZE;
    _qCount++;
}

// ============================================================
//  _calibrateTouch() — autocalibración de un sensor táctil
// ============================================================

void Input::_calibrateTouch(TouchState& t, uint8_t pin, const char* label) {
    const uint16_t N = TOUCH_MUESTRAS_CALIB;
    uint32_t muestras[N];

    Serial.printf("[input] calibrando %s...\n", label);

    for (uint16_t i = 0; i < N; i++) {
        muestras[i] = touchRead(pin);
        delay(10);
    }

    // Insertion sort
    for (uint16_t i = 1; i < N; i++) {
        uint32_t key = muestras[i];
        int16_t  j   = i - 1;
        while (j >= 0 && muestras[j] > key) {
            muestras[j + 1] = muestras[j];
            j--;
        }
        muestras[j + 1] = key;
    }

    uint16_t recorte = N / 10;
    if (recorte == 0) recorte = 1;

    uint64_t suma  = 0;
    uint16_t count = 0;
    for (uint16_t i = recorte; i < N - recorte; i++) {
        suma += muestras[i];
        count++;
    }

    t.baseline    = (count > 0) ? (uint32_t)(suma / count) : muestras[N / 2];
    t.threshHigh  = (uint32_t)(t.baseline * TOUCH_FACTOR_UMBRAL);
    float factorBajo = 1.0f + (TOUCH_FACTOR_UMBRAL - 1.0f) * 0.6f;
    t.threshLow   = (uint32_t)(t.baseline * factorBajo);
    t.lastValue   = muestras[N / 2];
    t.lastPollMs  = 0;
    t.isTouching  = false;
    t.consecHigh  = 0;
    t.consecLow   = 0;

    Serial.printf("[input] %s baseline=%lu umbral=%lu\n",
                  label, (unsigned long)t.baseline, (unsigned long)t.threshHigh);
}

// ============================================================
//  begin() — configuración de pines y calibración
// ============================================================

void Input::begin() {
    _qHead  = 0;
    _qTail  = 0;
    _qCount = 0;

    pinMode(PIN_BTN_A, INPUT_PULLUP);
    _btnA = { false, true, 0 };

    _calibrateTouch(_touch,    PIN_TOUCH,     "touch-cabeza");
    _calibrateTouch(_touchPie, PIN_TOUCH_PIE, "touch-pie");
}

// ============================================================
//  _pollBtn() — debounce y detección de flanco
// ============================================================

void Input::_pollBtn(BtnState& btn, uint8_t pin, InputEvent evPress,
                     const char* label, uint32_t now) {
    bool rawActual = (digitalRead(pin) == HIGH);

    if (rawActual != btn.raw) {
        btn.raw          = rawActual;
        btn.lastChangeMs = now;
    }

    if ((now - btn.lastChangeMs) >= DEBOUNCE_MS) {
        bool nuevoEstado = !btn.raw;

        if (nuevoEstado && !btn.debounced) {
            btn.debounced = true;
            _enqueue(evPress);
            Serial.printf("[input] %s presionado\n", label);
        } else if (!nuevoEstado && btn.debounced) {
            btn.debounced = false;
        }
    }
}

// ============================================================
//  _pollTouch() — muestreo con confirmación para un sensor
// ============================================================

void Input::_pollTouch(TouchState& t, uint8_t pin,
                       InputEvent evStart, InputEvent evEnd,
                       bool emitEnd, uint32_t now) {
    if ((now - t.lastPollMs) < TOUCH_POLL_MS) return;
    t.lastPollMs = now;

    uint32_t val = touchRead(pin);
    t.lastValue  = val;

    if (!t.isTouching) {
        if (val > t.threshHigh) {
            t.consecHigh++;
            t.consecLow = 0;
            if (t.consecHigh >= TOUCH_LECTURAS_CONFIRMA) {
                t.isTouching = true;
                t.consecHigh = 0;
                _enqueue(evStart);
                Serial.printf("[input] %s inicio (val=%lu)\n",
                              pin == PIN_TOUCH ? "TOUCH" : "TICKLE",
                              (unsigned long)val);
            }
        } else {
            t.consecHigh = 0;
        }
    } else {
        if (val <= t.threshLow) {
            t.consecLow++;
            t.consecHigh = 0;
            if (t.consecLow >= TOUCH_LECTURAS_CONFIRMA) {
                t.isTouching = false;
                t.consecLow  = 0;
                if (emitEnd) _enqueue(evEnd);
                Serial.printf("[input] %s fin\n",
                              pin == PIN_TOUCH ? "TOUCH" : "TICKLE");
            }
        } else {
            t.consecLow = 0;
        }
    }
}

// ============================================================
//  poll() — llamar en cada pasada del loop()
// ============================================================

void Input::poll(uint32_t now) {
    _pollBtn(_btnA, PIN_BTN_A, InputEvent::BTN_A_PRESS, "BTN_A", now);
    // Caricia en la cabeza: emite START y END
    _pollTouch(_touch,    PIN_TOUCH,     InputEvent::TOUCH_START,  InputEvent::TOUCH_END,   true,  now);
    // Cosquillas en el pie: solo emite START (cada toque cuenta como un evento)
    _pollTouch(_touchPie, PIN_TOUCH_PIE, InputEvent::TICKLE_START, InputEvent::NONE,        false, now);
}

// ============================================================
//  nextEvent()
// ============================================================

InputEvent Input::nextEvent() {
    if (_qCount == 0) return InputEvent::NONE;
    InputEvent ev = _queue[_qHead];
    _qHead  = (_qHead + 1) % QUEUE_SIZE;
    _qCount--;
    return ev;
}

// ============================================================
//  Accessors
// ============================================================

bool     Input::btnA()             const { return _btnA.debounced;       }
bool     Input::touching()         const { return _touch.isTouching;     }
bool     Input::touchingPie()      const { return _touchPie.isTouching;  }
uint32_t Input::touchValue()       const { return _touch.lastValue;      }
uint32_t Input::touchBaseline()    const { return _touch.baseline;       }
uint32_t Input::touchValuePie()    const { return _touchPie.lastValue;   }
uint32_t Input::touchBaselinePie() const { return _touchPie.baseline;    }
