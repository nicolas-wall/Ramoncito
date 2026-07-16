#pragma once
#include <Arduino.h>

// =============================================================
//  imu.h — Módulo IMU (MPU6050 por Wire crudo) para espToy
//
//  Lee aceleración del MPU6050 en 0x69 (AD0 a 3V3) compartiendo
//  el bus I2C con el OLED y el RTC DS3231.
//  No usa librería externa: solo registros por Wire.
//
//  Expone dos eventos consumibles por main.cpp:
//    imu.huboSacudida()  — la mascota fue sacudida
//    imu.huboLevantado() — la mascota fue levantada/alzada
//  Cada función retorna true UNA sola vez y luego limpia el flag
//  (patrón poll-and-clear, igual que InputEvent en input.h).
//
//  Si el MPU no responde en el boot, el módulo queda deshabilitado
//  silenciosamente: huboSacudida() / huboLevantado() devuelven
//  siempre false. El resto del firmware sigue funcionando.
// =============================================================

class IMU {
public:
    // Inicializa el MPU6050: despierta del sleep, configura rango ±2g.
    // Debe llamarse DESPUÉS de Wire.begin() (el OLED ya lo inicializó).
    void begin();

    // Llama en cada frame; lee aceleración y actualiza el estado de gestos.
    // Recibe el mismo timestamp `ahora` (millis) que usan todos los módulos.
    void update(uint32_t ahora);

    // ── Eventos poll-and-clear ──────────────────────────────────
    // Devuelve true si hubo sacudida desde la última consulta y la limpia.
    bool huboSacudida();

    // Devuelve true si la mascota fue levantada desde la última consulta.
    bool huboLevantado();

    // ── Estado de diagnóstico ───────────────────────────────────
    bool habilitado() const { return _habilitado; }

    // Magnitud de aceleración del último update (en unidades raw del ADC ÷ 16384 = g)
    float magnitudG() const { return _magnitudG; }

    // Número de sacudidas detectadas en la ventana actual
    uint8_t sacudidasEnVentana() const { return _sacudidasEnVentana; }

private:
    bool     _habilitado        = false;

    // Lectura cruda del acelerómetro (unidades: LSB, ±2g → 16384 LSB/g)
    int16_t  _ax = 0, _ay = 0, _az = 0;

    // Magnitud total de aceleración en g
    float    _magnitudG         = 0.0f;

    // ── Sacudida ─────────────────────────────────────────────────
    uint32_t _ultimaSacudidaMs  = 0;    // timestamp del último pico detectado
    uint32_t _ventanaInicioMs   = 0;    // inicio de la ventana de conteo
    uint8_t  _sacudidasEnVentana = 0;   // golpes dentro de la ventana actual

    // Flag de evento para huboSacudida() — lo setea update(), lo limpia huboSacudida()
    bool     _flagSacudida      = false;

    // ── Levantado por orientación sostenida ──────────────────────
    // Vector de gravedad filtrado por EMA (low-pass); se estabiliza
    // en los primeros segundos al arrancar.
    float    _gravX             = 0.0f;
    float    _gravY             = 0.0f;
    float    _gravZ             = 1.0f;  // arranca apuntando a gravedad nominal

    // Baseline de orientación de reposo (se adapta muy lento cuando quieto)
    float    _baseX             = 0.0f;
    float    _baseY             = 0.0f;
    float    _baseZ             = 1.0f;

    // Timestamp desde el cual la orientación difiere del baseline de forma sostenida
    uint32_t _levantadoDesdeMs  = 0;    // 0 = orientación dentro de la zona de reposo
    bool     _enZonaLevantado   = false; // true si el ángulo supera el umbral

    uint32_t _ultimoLevantadoMs = 0;    // debounce de detección
    bool     _flagLevantado     = false;

    // Indica si el vector de gravedad ya está inicializado (primeros frames)
    bool     _gravInicializada  = false;
    uint8_t  _warmupFrames      = 0;    // contador de frames de calentamiento

    // ── Helpers internos ──────────────────────────────────────────
    bool _leerRegistros();          // lee 6 bytes de aceleración por Wire
    bool _escribirRegistro(uint8_t reg, uint8_t val);
    bool _leerByte(uint8_t reg, uint8_t &out);
};

// Instancia global (igual que face, sound, personality…)
extern IMU imu;
