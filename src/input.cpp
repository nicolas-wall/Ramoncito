// ============================================================
//  input.cpp — Módulo de entradas: botones y touch capacitivo
//  Seeed XIAO ESP32-S3, framework Arduino
//
//  IMPORTANTE — comportamiento del touch en ESP32-S3:
//    touchRead() devuelve valores GRANDES en reposo (20000-30000)
//    y AUMENTAN al tocar. Contrario al ESP32 original.
//    Umbral: valor > baseline * TOUCH_FACTOR_UMBRAL  →  toque detectado.
// ============================================================
#include "input.h"
#include "config.h"

// Instancia global accesible desde main.cpp
Input input;

// Duración del combo A+B sostenido para el minijuego oculto (doc 04 §1).
// Local a este módulo a propósito: config.h lo edita otro.
static const uint32_t COMBO_AB_MS = 3000;

// ============================================================
//  Helpers de cola FIFO
// ============================================================

void Input::_enqueue(InputEvent ev) {
    if (_qCount >= QUEUE_SIZE) {
        // Cola llena: descartar el evento más viejo (avanzar head)
        _qHead = (_qHead + 1) % QUEUE_SIZE;
        _qCount--;
    }
    _queue[_qTail] = ev;
    _qTail  = (_qTail + 1) % QUEUE_SIZE;
    _qCount++;
}

// ============================================================
//  begin() — configuración de pines y autocalibración del touch
// ============================================================

void Input::begin() {
    // --- Inicializar cola ---
    _qHead  = 0;
    _qTail  = 0;
    _qCount = 0;

    // --- Configurar pines de botones ---
    pinMode(PIN_BTN_A, INPUT_PULLUP);
    pinMode(PIN_BTN_B, INPUT_PULLUP);

    // Estado inicial de botones (no presionados, HIGH por pull-up)
    _btnA = { false, true, 0 };
    _btnB = { false, true, 0 };

    // Estado inicial del combo A+B
    _comboStartMs = 0;
    _comboEmitted = false;

    // --- Autocalibración del touch ---
    // Tomar TOUCH_MUESTRAS_CALIB lecturas espaciadas ~10 ms,
    // descartar el 10% más alto y el 10% más bajo (outliers),
    // promediar el resto → baseline.

    const uint16_t N = TOUCH_MUESTRAS_CALIB;
    uint32_t muestras[N];

    Serial.println("[input] calibrando touch...");

    for (uint16_t i = 0; i < N; i++) {
        muestras[i] = touchRead(PIN_TOUCH);
        delay(10);  // delay() permitido en begin() / setup()
    }

    // Ordenar con insertion sort (N es pequeño, ≤50)
    for (uint16_t i = 1; i < N; i++) {
        uint32_t key = muestras[i];
        int16_t  j   = i - 1;
        while (j >= 0 && muestras[j] > key) {
            muestras[j + 1] = muestras[j];
            j--;
        }
        muestras[j + 1] = key;
    }

    // Descartar 10% inferior y 10% superior
    uint16_t recorte = N / 10;  // ej. 50 → 5 de cada lado
    if (recorte == 0) recorte = 1;

    uint64_t suma  = 0;
    uint16_t count = 0;
    for (uint16_t i = recorte; i < N - recorte; i++) {
        suma += muestras[i];
        count++;
    }

    _touchBaseline = (count > 0) ? (uint32_t)(suma / count) : muestras[N / 2];

    // Umbral alto: baseline * TOUCH_FACTOR_UMBRAL
    _touchThreshHigh = (uint32_t)(_touchBaseline * TOUCH_FACTOR_UMBRAL);

    // Umbral bajo con histéresis: baseline * (1 + (factor-1)*0.6)
    // Ej. con factor 1.15: umbral bajo = baseline * 1.09
    float factorBajo = 1.0f + (TOUCH_FACTOR_UMBRAL - 1.0f) * 0.6f;
    _touchThreshLow  = (uint32_t)(_touchBaseline * factorBajo);

    // Estado inicial del touch
    _touchLastValue   = muestras[N / 2];
    _touchLastPollMs  = 0;
    _isTouching       = false;
    _touchConsecHigh  = 0;
    _touchConsecLow   = 0;

    Serial.printf("[input] touch baseline=%lu umbral=%lu\n",
                  (unsigned long)_touchBaseline,
                  (unsigned long)_touchThreshHigh);
}

// ============================================================
//  _pollBtn() — lógica de debounce y detección de flanco
// ============================================================

void Input::_pollBtn(BtnState& btn, uint8_t pin, InputEvent evPress,
                     const char* label, uint32_t now) {
    bool rawActual = (digitalRead(pin) == HIGH);  // HIGH = no presionado (pull-up activo en BAJO)

    if (rawActual != btn.raw) {
        // Hubo un cambio: reiniciar temporizador de estabilidad
        btn.raw          = rawActual;
        btn.lastChangeMs = now;
    }

    // Aceptar cambio solo si el pin se mantuvo estable DEBOUNCE_MS
    if ((now - btn.lastChangeMs) >= DEBOUNCE_MS) {
        bool nuevoEstado = !btn.raw;  // true = presionado (LOW → true)

        if (nuevoEstado && !btn.debounced) {
            // Flanco de presión (HIGH→LOW debounced): emitir evento
            btn.debounced = true;
            _enqueue(evPress);
            Serial.printf("[input] %s presionado\n", label);
        } else if (!nuevoEstado && btn.debounced) {
            // Soltar: actualizar estado interno, no emitir evento (por ahora)
            btn.debounced = false;
        }
    }
}

// ============================================================
//  _pollCombo() — combo secreto A+B sostenido >= COMBO_AB_MS
// ============================================================
//  El contador arranca cuando el segundo botón se suma (ambos
//  debounced presionados). Se resetea si cualquiera se suelta.
//  Emite COMBO_AB_3S una sola vez; para volver a emitir hay que
//  soltar ambos y repetir el gesto. No suprime los BTN_x_PRESS
//  normales (main resuelve la prioridad menú/combo).

void Input::_pollCombo(uint32_t now) {
    bool ambos = _btnA.debounced && _btnB.debounced;

    if (!ambos) {
        // Al menos uno suelto: resetear contador.
        _comboStartMs = 0;
        // Rearmar solo cuando AMBOS están sueltos.
        if (!_btnA.debounced && !_btnB.debounced) {
            _comboEmitted = false;
        }
        return;
    }

    // Ambos presionados: arrancar el contador si no estaba corriendo.
    if (_comboStartMs == 0) {
        _comboStartMs = now;
    }

    // Emitir una sola vez al cumplirse la duración.
    if (!_comboEmitted && (now - _comboStartMs) >= COMBO_AB_MS) {
        _comboEmitted = true;
        _enqueue(InputEvent::COMBO_AB_3S);
        Serial.println("[input] COMBO A+B 3s");
    }
}

// ============================================================
//  _pollTouch() — muestreo y detección con confirmación
// ============================================================

void Input::_pollTouch(uint32_t now) {
    // Muestrear como máximo cada TOUCH_POLL_MS
    if ((now - _touchLastPollMs) < TOUCH_POLL_MS) return;
    _touchLastPollMs = now;

    uint32_t val      = touchRead(PIN_TOUCH);
    _touchLastValue   = val;

    if (!_isTouching) {
        // Esperando inicio de toque
        if (val > _touchThreshHigh) {
            _touchConsecHigh++;
            _touchConsecLow = 0;
            if (_touchConsecHigh >= TOUCH_LECTURAS_CONFIRMA) {
                _isTouching      = true;
                _touchConsecHigh = 0;
                _enqueue(InputEvent::TOUCH_START);
                Serial.printf("[input] TOUCH inicio (valor=%lu)\n",
                              (unsigned long)val);
            }
        } else {
            _touchConsecHigh = 0;
        }
    } else {
        // Esperando fin de toque (umbral bajo con histéresis)
        if (val <= _touchThreshLow) {
            _touchConsecLow++;
            _touchConsecHigh = 0;
            if (_touchConsecLow >= TOUCH_LECTURAS_CONFIRMA) {
                _isTouching     = false;
                _touchConsecLow = 0;
                _enqueue(InputEvent::TOUCH_END);
                Serial.println("[input] TOUCH fin");
            }
        } else {
            _touchConsecLow = 0;
        }
    }
}

// ============================================================
//  poll() — llamar en cada pasada del loop()
// ============================================================

void Input::poll(uint32_t now) {
    _pollBtn(_btnA, PIN_BTN_A, InputEvent::BTN_A_PRESS,
             "BTN_A (D0/GPIO1)", now);
    _pollBtn(_btnB, PIN_BTN_B, InputEvent::BTN_B_PRESS,
             "BTN_B (D1/GPIO2)", now);
    _pollCombo(now);
    _pollTouch(now);
}

// ============================================================
//  nextEvent() — consumir el próximo evento de la cola
// ============================================================

InputEvent Input::nextEvent() {
    if (_qCount == 0) return InputEvent::NONE;
    InputEvent ev = _queue[_qHead];
    _qHead  = (_qHead + 1) % QUEUE_SIZE;
    _qCount--;
    return ev;
}

// ============================================================
//  Accessors de estado crudo
// ============================================================

bool     Input::btnA()          const { return _btnA.debounced;  }
bool     Input::btnB()          const { return _btnB.debounced;  }
bool     Input::touching()      const { return _isTouching;      }
uint32_t Input::touchValue()    const { return _touchLastValue;  }
uint32_t Input::touchBaseline() const { return _touchBaseline;   }
