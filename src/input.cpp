// ============================================================
//  input.cpp — Entradas del arcade: botones + palanca analógica
//  Seeed XIAO ESP32-S3, framework Arduino
//
//  La palanca es un par de potenciómetros: cada eje entrega una tensión
//  de 0 a 3.3 V que el ADC1 lee como 0..4095, con el reposo cerca de la
//  mitad. "Cerca" y no "exactamente": cada unidad tiene su propio centro,
//  por eso se calibra al bootear en vez de asumir 2048.
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
//  _calibrarEje() — centro de reposo de un eje
//  Promedio simple de JOY_MUESTRAS_CALIB lecturas. Importante:
//  no tocar la palanca durante el boot.
// ============================================================

uint16_t Input::_calibrarEje(uint8_t pin, const char* label) {
    uint32_t suma = 0;
    for (uint16_t i = 0; i < JOY_MUESTRAS_CALIB; i++) {
        suma += (uint16_t)analogRead(pin);
        delay(2);
    }
    uint16_t centro = (uint16_t)(suma / JOY_MUESTRAS_CALIB);
    Serial.printf("[input] eje %s centro=%u\n", label, centro);
    return centro;
}

// ============================================================
//  begin() — configuración de pines y calibración
// ============================================================

void Input::begin() {
    _qHead  = 0;
    _qTail  = 0;
    _qCount = 0;

    // Todos los botones a GND con pull-up interno. Un pin sin botón
    // conectado queda en HIGH = suelto, así que sobra con no cablearlos.
    pinMode(PIN_BTN_A,  INPUT_PULLUP);
    pinMode(PIN_BTN_B,  INPUT_PULLUP);
    pinMode(PIN_BTN_C,  INPUT_PULLUP);
    pinMode(PIN_JOY_SW, INPUT_PULLUP);

    _btnA = _btnB = _btnC = _joySw = { false, true, 0 };

    _ejeX = { 0, 0, 0.0f, 0, 0 };
    _ejeY = { 0, 0, 0.0f, 0, 0 };
    _lastJoyPollMs = 0;
    _stubX = _stubY = 0.0f;
    _stubHasta = 0;

    if (JOYSTICK_PRESENTE) {
        analogReadResolution(12);
        // 11 dB = fondo de escala ~3.3 V, que es el rango de la palanca.
        analogSetPinAttenuation(PIN_JOY_X, ADC_11db);
        analogSetPinAttenuation(PIN_JOY_Y, ADC_11db);
        _ejeX.centro = _calibrarEje(PIN_JOY_X, "X");
        _ejeY.centro = _calibrarEje(PIN_JOY_Y, "Y");
    } else {
        // Sin palanca: centro nominal, y los ejes se alimentan del stub.
        _ejeX.centro = _ejeY.centro = JOY_ADC_MAX / 2;
        Serial.println("[input] palanca AUSENTE — ejes emulados por serial (w/a/s/d)");
    }
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
        bool nuevoEstado = !btn.raw;   // pull-up: LOW = presionado

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
//  _normalizar() — ADC crudo → -1..+1 con zona muerta
//
//  Los dos lados del recorrido se escalan por separado porque el centro
//  calibrado casi nunca cae justo en la mitad: si se usara un único
//  divisor, un lado llegaría a 1.0 antes que el otro.
// ============================================================

float Input::_normalizar(const AxisState& eje, uint16_t raw, bool invertir) const {
    float v;
    if (raw >= eje.centro) {
        uint16_t span = (uint16_t)(JOY_ADC_MAX - eje.centro);
        v = (span > 0) ? (float)(raw - eje.centro) / (float)span : 0.0f;
    } else {
        uint16_t span = eje.centro;
        v = (span > 0) ? -(float)(eje.centro - raw) / (float)span : 0.0f;
    }

    if (invertir) v = -v;
    if (v >  1.0f) v =  1.0f;
    if (v < -1.0f) v = -1.0f;

    // Zona muerta con reescalado: apenas se sale de la deadzone el valor
    // arranca en 0 y no salta a JOY_DEADZONE (si no, el control da un tirón).
    float mag = fabsf(v);
    if (mag < JOY_DEADZONE) return 0.0f;
    float escalado = (mag - JOY_DEADZONE) / (1.0f - JOY_DEADZONE);
    return (v < 0) ? -escalado : escalado;
}

// ============================================================
//  _pollEje() — valor continuo → dirección discreta + auto-repeat
// ============================================================

void Input::_pollEje(AxisState& eje, float valor,
                     InputEvent evNeg, InputEvent evPos, uint32_t now) {
    eje.value = valor;
    float mag = fabsf(valor);

    if (eje.dir == 0) {
        // Suelto: activar solo al superar el umbral alto.
        if (mag >= JOY_UMBRAL_ON) {
            eje.dir          = (valor < 0) ? -1 : 1;
            eje.nextRepeatMs = now + JOY_REPEAT_DELAY_MS;
            _enqueue(eje.dir < 0 ? evNeg : evPos);
        }
        return;
    }

    // Sostenido: soltar al bajar del umbral bajo o al cruzar al otro lado.
    bool mismoLado = (eje.dir < 0) ? (valor < 0) : (valor > 0);
    if (mag < JOY_UMBRAL_OFF || !mismoLado) {
        eje.dir = 0;
        return;
    }

    if ((int32_t)(now - eje.nextRepeatMs) >= 0) {
        eje.nextRepeatMs = now + JOY_REPEAT_MS;
        _enqueue(eje.dir < 0 ? evNeg : evPos);
    }
}

// ============================================================
//  poll() — llamar en cada pasada del loop()
// ============================================================

void Input::poll(uint32_t now) {
    _pollBtn(_btnA,  PIN_BTN_A,  InputEvent::BTN_A_PRESS,  "BTN_A",  now);
    _pollBtn(_btnB,  PIN_BTN_B,  InputEvent::BTN_B_PRESS,  "BTN_B",  now);
    _pollBtn(_btnC,  PIN_BTN_C,  InputEvent::BTN_C_PRESS,  "BTN_C",  now);
    _pollBtn(_joySw, PIN_JOY_SW, InputEvent::JOY_SW_PRESS, "JOY_SW", now);

    if ((now - _lastJoyPollMs) < JOY_POLL_MS) return;
    _lastJoyPollMs = now;

    float vx, vy;
    if (JOYSTICK_PRESENTE) {
        _ejeX.raw = (uint16_t)analogRead(PIN_JOY_X);
        _ejeY.raw = (uint16_t)analogRead(PIN_JOY_Y);
        vx = _normalizar(_ejeX, _ejeX.raw, JOY_INVERTIR_X);
        vy = _normalizar(_ejeY, _ejeY.raw, JOY_INVERTIR_Y);
    } else {
        // Stub: el "golpe" de tecla vuelve solo al centro.
        if (_stubHasta != 0 && (int32_t)(now - _stubHasta) >= 0) {
            _stubX = _stubY = 0.0f;
            _stubHasta = 0;
        }
        vx = _stubX;
        vy = _stubY;
        _ejeX.raw = _ejeX.centro;
        _ejeY.raw = _ejeY.centro;
    }

    _pollEje(_ejeX, vx, InputEvent::JOY_LEFT, InputEvent::JOY_RIGHT, now);
    _pollEje(_ejeY, vy, InputEvent::JOY_UP,   InputEvent::JOY_DOWN,  now);
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
//  Stub de la palanca (mientras no esté el hardware)
// ============================================================

void Input::setStubAxis(float x, float y, uint32_t now) {
    if (JOYSTICK_PRESENTE) return;
    _stubX     = x;
    _stubY     = y;
    _stubHasta = now + JOY_STUB_AUTOCENTRO_MS;
}

void Input::injectEvent(InputEvent ev) {
    _enqueue(ev);
}

// ============================================================
//  Accessors
// ============================================================

bool  Input::btnA()   const { return _btnA.debounced;  }
bool  Input::btnB()   const { return _btnB.debounced;  }
bool  Input::btnC()   const { return _btnC.debounced;  }
bool  Input::joySw()  const { return _joySw.debounced; }
bool  Input::anyBtn() const {
    return _btnA.debounced || _btnB.debounced || _btnC.debounced || _joySw.debounced;
}

float Input::axisX() const { return _ejeX.value; }
float Input::axisY() const { return _ejeY.value; }

bool  Input::left()  const { return _ejeX.dir < 0; }
bool  Input::right() const { return _ejeX.dir > 0; }
bool  Input::up()    const { return _ejeY.dir < 0; }
bool  Input::down()  const { return _ejeY.dir > 0; }

bool     Input::joystickPresente() const { return JOYSTICK_PRESENTE; }
uint16_t Input::rawX()    const { return _ejeX.raw;    }
uint16_t Input::rawY()    const { return _ejeY.raw;    }
uint16_t Input::centroX() const { return _ejeX.centro; }
uint16_t Input::centroY() const { return _ejeY.centro; }
