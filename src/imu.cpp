// =============================================================
//  imu.cpp — Módulo IMU para Ramoncito (MPU6050 por Wire crudo)
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
    // No exigimos 0x68: muchos módulos baratos traen clones (MPU6500/9250 y
    // similares) que devuelven otro WHO_AM_I (0x70, 0x71, 0x73, 0x75, ...) pero
    // comparten el mapa de registros de aceleración, así que funcionan igual.
    // Solo rechazamos 0x00 / 0xFF, que indican bus muerto o chip ausente.
    if (whoAmI == 0x00 || whoAmI == 0xFF) {
        Serial.printf("[imu] WHO_AM_I invalido: 0x%02X — modulo deshabilitado\n", whoAmI);
        _habilitado = false;
        return;
    }
    if (whoAmI != MPU_WHO_AM_I_VALUE) {
        Serial.printf("[imu] WHO_AM_I 0x%02X (clon, no 0x68) — se usa igual\n", whoAmI);
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

    // ── Filtro EMA sobre el vector de aceleración (low-pass de gravedad) ─
    // Se inicializa con las primeras lecturas para que el baseline parta
    // de la orientación real de posado, no de valores arbitrarios.
    if (!_gravInicializada) {
        if (_warmupFrames < IMU_GRAV_WARMUP_FRAMES) {
            // Promedio simple de los primeros frames de calentamiento
            float w = 1.0f / (float)(_warmupFrames + 1);
            _gravX = _gravX + (ax - _gravX) * w;
            _gravY = _gravY + (ay - _gravY) * w;
            _gravZ = _gravZ + (az - _gravZ) * w;
            _warmupFrames++;
        } else {
            // Copiar al baseline inicial y marcar inicializado
            _baseX = _gravX;
            _baseY = _gravY;
            _baseZ = _gravZ;
            _gravInicializada = true;
            Serial.printf("[imu] grav baseline init: (%.2f, %.2f, %.2f)\n",
                          _baseX, _baseY, _baseZ);
        }
    } else {
        // EMA rápido para seguir la gravedad instantánea
        _gravX += (ax - _gravX) * IMU_GRAV_ALPHA;
        _gravY += (ay - _gravY) * IMU_GRAV_ALPHA;
        _gravZ += (az - _gravZ) * IMU_GRAV_ALPHA;
    }

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

    // ── Detección de LEVANTADO por orientación sostenida ─────
    // Comparamos el vector de gravedad filtrado actual contra el baseline
    // de reposo usando el producto punto (coseno del ángulo entre vectores).
    // Cuando el coseno cae por debajo del umbral, el ángulo supera el límite
    // configurado → la orientación cambió significativamente (levantaron/giraron
    // el juguete).  Para evitar falsas alarmas por sacudidas:
    //   • sacudir no desplaza la gravedad filtrada de forma sostenida (el EMA
    //     no alcanza a seguir picos breves), así que el coseno se recupera rápido.
    //   • sí exigimos que el desvío se mantenga IMU_LEVANTADO_SOSTEN_MS antes
    //     de emitir el evento.
    if (_gravInicializada) {
        // Normalizar el vector de gravedad actual
        float magGrav = sqrtf(_gravX*_gravX + _gravY*_gravY + _gravZ*_gravZ);
        float magBase = sqrtf(_baseX*_baseX + _baseY*_baseY + _baseZ*_baseZ);

        float cosAngulo = 1.0f;  // default: sin desvío
        if (magGrav > 0.1f && magBase > 0.1f) {
            cosAngulo = (_gravX*_baseX + _gravY*_baseY + _gravZ*_baseZ)
                        / (magGrav * magBase);
            // Clampear por posibles errores de redondeo
            if (cosAngulo >  1.0f) cosAngulo =  1.0f;
            if (cosAngulo < -1.0f) cosAngulo = -1.0f;
        }

        // Coseno del umbral de ángulo (precomputado conceptualmente; el
        // compilador lo optimiza a constante porque IMU_LEVANTADO_ANGULO_GRADOS
        // es literal de config.h).
        float cosUmbral = cosf(IMU_LEVANTADO_ANGULO_GRADOS * (3.14159265f / 180.0f));

        bool anguloDiferente = (cosAngulo < cosUmbral);

        if (anguloDiferente) {
            if (!_enZonaLevantado) {
                // Acaba de entrar en zona de desvío → arrancar temporizador
                _enZonaLevantado  = true;
                _levantadoDesdeMs = ahora;
            }
            // Verificar si se sostuvo suficiente tiempo
            bool sostenido = (ahora - _levantadoDesdeMs) >= IMU_LEVANTADO_SOSTEN_MS;
            bool fueraDebounceLevantado = (ahora - _ultimoLevantadoMs) >= IMU_LEVANTADO_DEBOUNCE_MS;

            if (sostenido && fueraDebounceLevantado && !_flagLevantado) {
                _ultimoLevantadoMs = ahora;
                _flagLevantado     = true;
                Serial.printf("[imu] levantado detectado | angulo>%.0f° (cos=%.3f) sostenido %ums\n",
                              IMU_LEVANTADO_ANGULO_GRADOS, cosAngulo, IMU_LEVANTADO_SOSTEN_MS);
            }
        } else {
            // Orientación volvió al baseline → salir de zona y adaptar baseline lento
            _enZonaLevantado  = false;
            _levantadoDesdeMs = 0;

            // Adaptar baseline muy lentamente solo cuando la magnitud es ~1g
            // (el juguete está quieto en su nueva posición o en reposo)
            bool magnitudReposo = (_magnitudG > 0.85f && _magnitudG < 1.15f);
            if (magnitudReposo) {
                _baseX += (_gravX - _baseX) * IMU_BASE_ALPHA;
                _baseY += (_gravY - _baseY) * IMU_BASE_ALPHA;
                _baseZ += (_gravZ - _baseZ) * IMU_BASE_ALPHA;
            }
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
