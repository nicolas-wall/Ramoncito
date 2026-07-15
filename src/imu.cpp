// =============================================================
//  imu.cpp — Módulo IMU para espToy (MPU6050 por Wire crudo)
//
//  Registros MPU6050 usados:
//    0x6B  PWR_MGMT_1   — escribir 0 para despertar del sleep
//    0x1C  ACCEL_CONFIG — rango ±2g (default, no hace falta escribir)
//    0x3B  ACCEL_XOUT_H — primer byte de las 6 lecturas de aceleración
//    0x75  WHO_AM_I     — debe responder 0x68 (valor fijo del chip,
//                         independiente del pin AD0 que fija la dirección I2C)
//
//  La dirección I2C usada es IMU_ADDR = 0x69 (AD0 a 3V3).
// =============================================================

#include "imu.h"
#include "config.h"
#include <Wire.h>
#include <math.h>

// Instancia global
IMU imu;

// ── Constantes de registro MPU6050 ───────────────────────────
static const uint8_t MPU_REG_PWR_MGMT_1  = 0x6B;
static const uint8_t MPU_REG_ACCEL_XOUT_H = 0x3B;
static const uint8_t MPU_REG_WHO_AM_I     = 0x75;
static const uint8_t MPU_WHO_AM_I_VALUE   = 0x68; // respuesta esperada (fija en el chip)

// ── begin() ──────────────────────────────────────────────────
void IMU::begin() {
    // 1. Verificar que el MPU responde en la dirección configurada
    uint8_t whoAmI = 0;
    if (!_leerByte(MPU_REG_WHO_AM_I, whoAmI)) {
        Serial.printf("[imu] no responde en 0x%02X — modulo deshabilitado\n", IMU_ADDR);
        _habilitado = false;
        return;
    }
    if (whoAmI != MPU_WHO_AM_I_VALUE) {
        Serial.printf("[imu] WHO_AM_I inesperado: 0x%02X (esperaba 0x%02X) — deshabilitado\n",
                      whoAmI, MPU_WHO_AM_I_VALUE);
        _habilitado = false;
        return;
    }

    // 2. Despertar del sleep (PWR_MGMT_1 = 0)
    if (!_escribirRegistro(MPU_REG_PWR_MGMT_1, 0x00)) {
        Serial.println("[imu] error escribiendo PWR_MGMT_1 — deshabilitado");
        _habilitado = false;
        return;
    }

    // 3. Pequeña pausa para que el oscilador interno estabilice
    delay(10);

    _habilitado = true;
    Serial.printf("[imu] MPU6050 OK en 0x%02X (WHO_AM_I=0x%02X)\n", IMU_ADDR, whoAmI);
}

// ── update() ─────────────────────────────────────────────────
void IMU::update(uint32_t ahora) {
    if (!_habilitado) return;
    if (!_leerRegistros()) return;

    // Magnitud total en g (LSB ÷ 16384 para rango ±2g)
    float ax = _ax / 16384.0f;
    float ay = _ay / 16384.0f;
    float az = _az / 16384.0f;
    _magnitudG = sqrtf(ax * ax + ay * ay + az * az);

    // ── Detección de SACUDIDA ─────────────────────────────────
    // Se activa cuando la magnitud supera el umbral (pico de impacto)
    // con debounce: no se cuenta otro pico si no pasó el debounce desde el último.
    bool hayPico = (_magnitudG > IMU_SACUDIDA_UMBRAL);
    bool fueraDebounceSacudida = (ahora - _ultimaSacudidaMs) >= IMU_SACUDIDA_DEBOUNCE_MS;

    if (hayPico && fueraDebounceSacudida) {
        _ultimaSacudidaMs = ahora;

        // Abrir / renovar ventana de conteo
        if (_sacudidasEnVentana == 0 || (ahora - _ventanaInicioMs) > IMU_SACUDIDA_VENTANA_MS) {
            // Nueva ventana
            _ventanaInicioMs    = ahora;
            _sacudidasEnVentana = 1;
        } else {
            _sacudidasEnVentana++;
        }

        Serial.printf("[imu] sacudida detectada | mag=%.2fg | en ventana:%u\n",
                      _magnitudG, _sacudidasEnVentana);

        // Emitir el evento de sacudida (main.cpp lo consumirá en el próximo frame)
        _flagSacudida = true;
    }

    // Cerrar ventana expirada (para que el conteo no se acumule indefinidamente)
    if (_sacudidasEnVentana > 0 && (ahora - _ventanaInicioMs) > IMU_SACUDIDA_VENTANA_MS) {
        _sacudidasEnVentana = 0;
    }

    // ── Detección de LEVANTADO ────────────────────────────────
    // Criterio simple y conservador: la magnitud está en la banda de
    // IMU_LEVANTADO_MIN..IMU_LEVANTADO_MAX (movimiento moderado, NO pico de sacudida)
    // y pasó suficiente tiempo desde el último levantado detectado.
    bool fueraDebounceLevantado = (ahora - _ultimoLevantadoMs) >= IMU_LEVANTADO_DEBOUNCE_MS;
    bool esMagnitudLevantado    = (_magnitudG >= IMU_LEVANTADO_MIN && _magnitudG <= IMU_LEVANTADO_MAX);

    if (esMagnitudLevantado && fueraDebounceLevantado && !_flagLevantado) {
        // Solo si NO es también un pico de sacudida (para no solapar eventos)
        if (!hayPico) {
            _ultimoLevantadoMs = ahora;
            _flagLevantado     = true;
            Serial.printf("[imu] levantado detectado | mag=%.2fg\n", _magnitudG);
        }
    }
}

// ── huboSacudida() — poll-and-clear ──────────────────────────
bool IMU::huboSacudida() {
    if (_flagSacudida) {
        _flagSacudida = false;
        return true;
    }
    return false;
}

// ── huboLevantado() — poll-and-clear ─────────────────────────
bool IMU::huboLevantado() {
    if (_flagLevantado) {
        _flagLevantado = false;
        return true;
    }
    return false;
}

// ── _leerRegistros() — lee los 6 bytes de aceleración ────────
bool IMU::_leerRegistros() {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(MPU_REG_ACCEL_XOUT_H);
    if (Wire.endTransmission(false) != 0) return false;

    uint8_t n = Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)6);
    if (n < 6) return false;

    _ax = (int16_t)((Wire.read() << 8) | Wire.read());
    _ay = (int16_t)((Wire.read() << 8) | Wire.read());
    _az = (int16_t)((Wire.read() << 8) | Wire.read());
    return true;
}

// ── _escribirRegistro() ───────────────────────────────────────
bool IMU::_escribirRegistro(uint8_t reg, uint8_t val) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(reg);
    Wire.write(val);
    return (Wire.endTransmission() == 0);
}

// ── _leerByte() ──────────────────────────────────────────────
bool IMU::_leerByte(uint8_t reg, uint8_t &out) {
    Wire.beginTransmission(IMU_ADDR);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom((uint8_t)IMU_ADDR, (uint8_t)1) < 1) return false;
    out = Wire.read();
    return true;
}
