// =============================================================
//  mood.cpp — Cerebro Tamagotchi de espToy
//  Estado: happiness, energy, boredom (0–100 c/u)
//  Decaimiento por ticks periódicos, efectos de interacción,
//  persistencia en NVS mediante Preferences.
// =============================================================

#include "mood.h"
#include "config.h"
#include <Preferences.h>
#include <time.h>

// Instancia global accesible desde main.cpp y otros módulos
Mood mood;

// ── Namespace y claves NVS ────────────────────────────────────
static const char* NVS_NS        = "esptoy";
static const char* NVS_HAPPINESS = "happiness";
static const char* NVS_ENERGY    = "energy";
static const char* NVS_BOREDOM   = "boredom";
static const char* NVS_EPOCH     = "lastEpoch";

// ─────────────────────────────────────────────────────────────
//  Helpers: aritmética saturada en [0, 100]
// ─────────────────────────────────────────────────────────────

uint8_t Mood::_addSat(uint8_t v, uint8_t delta) {
    uint16_t r = (uint16_t)v + delta;
    return (r > 100u) ? 100u : (uint8_t)r;
}

uint8_t Mood::_subSat(uint8_t v, uint8_t delta) {
    return (delta >= v) ? 0u : (v - delta);
}

// ─────────────────────────────────────────────────────────────
//  Persistencia NVS
// ─────────────────────────────────────────────────────────────

void Mood::_loadFromNVS() {
    Preferences prefs;
    // Abrir en modo solo lectura
    if (!prefs.begin(NVS_NS, /*readOnly=*/true)) {
        // Namespace aún no existe: usar defaults
        _happiness = MOOD_INI_FELICIDAD;
        _energy    = MOOD_INI_ENERGIA;
        _boredom   = MOOD_INI_ABURRIM;
        _lastKnownEpoch = 0;
        Serial.println("[mood] NVS vacío, usando defaults");
        return;
    }

    // Si la clave no existe, getUChar devuelve el valor por defecto pasado
    bool hasData = prefs.isKey(NVS_HAPPINESS);
    if (hasData) {
        _happiness      = prefs.getUChar(NVS_HAPPINESS, MOOD_INI_FELICIDAD);
        _energy         = prefs.getUChar(NVS_ENERGY,    MOOD_INI_ENERGIA);
        _boredom        = prefs.getUChar(NVS_BOREDOM,   MOOD_INI_ABURRIM);
        _lastKnownEpoch = (time_t)prefs.getLong64(NVS_EPOCH, 0);
        Serial.printf("[mood] cargado NVS -> F:%u E:%u A:%u epoch:%lld\n",
                      _happiness, _energy, _boredom, (long long)_lastKnownEpoch);
    } else {
        _happiness      = MOOD_INI_FELICIDAD;
        _energy         = MOOD_INI_ENERGIA;
        _boredom        = MOOD_INI_ABURRIM;
        _lastKnownEpoch = 0;
        Serial.println("[mood] sin datos NVS, usando defaults");
    }
    prefs.end();
}

void Mood::_saveToNVS() {
    Preferences prefs;
    if (!prefs.begin(NVS_NS, /*readOnly=*/false)) {
        Serial.println("[mood] ERROR: no se pudo abrir NVS para escritura");
        return;
    }
    prefs.putUChar(NVS_HAPPINESS, _happiness);
    prefs.putUChar(NVS_ENERGY,    _energy);
    prefs.putUChar(NVS_BOREDOM,   _boredom);

    // Guardar epoch solo si alguna vez recibimos hora válida
    if (_timeValid) {
        // Actualizar epoch estimado: si el sistema tiene hora válida, usarla
        // directamente; si no, mantener el último conocido.
        time_t now = time(nullptr);
        if (now > 1600000000L) {
            _lastKnownEpoch = now;
        }
        prefs.putLong64(NVS_EPOCH, (int64_t)_lastKnownEpoch);
    }

    prefs.end();
    _dirty       = false;
    _lastSaveMs  = millis();
}

// ─────────────────────────────────────────────────────────────
//  begin(): inicialización desde NVS o defaults
// ─────────────────────────────────────────────────────────────

void Mood::begin() {
    _dirty           = false;
    _timeValid       = false;
    _lastKnownEpoch  = 0;
    _lastTickMs      = millis();
    _lastSaveMs      = millis();

    _loadFromNVS();
}

// ─────────────────────────────────────────────────────────────
//  update(): decaimiento temporal + guardado diferido
//  Llamar cada frame (FRAME_MS ≈ 33 ms).
//  El tick de decaimiento ocurre cada MOOD_TICK_MS / TIME_SCALE ms.
// ─────────────────────────────────────────────────────────────

void Mood::update(uint32_t now) {
    // Intervalo entre ticks de decaimiento (escalado para pruebas)
    const uint32_t tickInterval = MOOD_TICK_MS / TIME_SCALE;

    // Verificar si pasó un tick de decaimiento
    if ((now - _lastTickMs) >= tickInterval) {
        _lastTickMs = now;

        // Decaimiento de energía
        _energy = _subSat(_energy, MOOD_DECAY_ENERGIA_PM);

        // Decaimiento de felicidad (doble si el aburrimiento es alto)
        uint8_t felicidadDecay = MOOD_DECAY_FELICIDAD_PM;
        if (_boredom > 70) {
            felicidadDecay = felicidadDecay * 2;
        }
        _happiness = _subSat(_happiness, felicidadDecay);

        // Aumento del aburrimiento
        _boredom = _addSat(_boredom, MOOD_SUBE_ABURRIM_PM);

        _dirty = true;
    }

    // Guardado diferido: si hay cambios y pasó suficiente tiempo desde el último guardado
    if (_dirty && (now - _lastSaveMs) >= MOOD_GUARDAR_CADA_MS) {
        _saveToNVS();
    }
}

// ─────────────────────────────────────────────────────────────
//  apply(): aplica un efecto de interacción y guarda en NVS
// ─────────────────────────────────────────────────────────────

void Mood::apply(MoodEffect e) {
    switch (e) {
        case MoodEffect::BTN_A:
            _happiness = _addSat(_happiness, 10);
            _boredom   = _subSat(_boredom,   5);
            break;

        case MoodEffect::BTN_B:
            _energy  = _addSat(_energy,  15);
            _boredom = _subSat(_boredom, 10);
            break;

        case MoodEffect::CARICIA:
            _happiness = _addSat(_happiness, 20);
            _boredom   = _subSat(_boredom,   15);
            break;

        case MoodEffect::JUGO_PONG_GANO_HUMANO:
            // La mascota perdió el pong → se ofende
            _happiness = _subSat(_happiness, 5);
            _boredom   = _subSat(_boredom,   40);
            break;

        case MoodEffect::JUGO_PONG_GANO_CPU:
            // La mascota ganó el pong
            _happiness = _addSat(_happiness, 10);
            _boredom   = _subSat(_boredom,   40);
            break;

        case MoodEffect::DESPERTADO_DE_NOCHE:
            _happiness = _subSat(_happiness, 5);
            break;
    }

    // Log del efecto con nombres legibles
    const char* nombre = "?";
    switch (e) {
        case MoodEffect::BTN_A:                 nombre = "BTN_A";                 break;
        case MoodEffect::BTN_B:                 nombre = "BTN_B";                 break;
        case MoodEffect::CARICIA:               nombre = "CARICIA";               break;
        case MoodEffect::JUGO_PONG_GANO_HUMANO: nombre = "JUGO_PONG_GANO_HUMANO"; break;
        case MoodEffect::JUGO_PONG_GANO_CPU:    nombre = "JUGO_PONG_GANO_CPU";    break;
        case MoodEffect::DESPERTADO_DE_NOCHE:   nombre = "DESPERTADO_DE_NOCHE";   break;
    }
    Serial.printf("[mood] %s -> F:%u E:%u A:%u\n", nombre, _happiness, _energy, _boredom);

    // Guardado inmediato
    _saveToNVS();
}

// ─────────────────────────────────────────────────────────────
//  dominantExpression(): tabla doc 03 §5 con orden corregido
//  La fila compuesta (felicidad < 20 Y aburrimiento > 60 → ENOJADO)
//  se evalúa ANTES que la simple (felicidad < 20 → TRISTE) para que
//  sea alcanzable; el doc las lista al revés, lo que haría ENOJADO
//  inalcanzable por esa vía.
// ─────────────────────────────────────────────────────────────

Expression Mood::dominantExpression() const {
    // 1. Energía crítica → dormido (prioridad máxima)
    if (_energy < 10) {
        return Expression::DORMIDO;
    }

    // 2. Condición compuesta primero: muy infeliz Y muy aburrido → enojado
    if (_happiness < 20 && _boredom > 60) {
        return Expression::ENOJADO;
    }

    // 3. Solo infeliz (aburrimiento no suficientemente alto) → triste
    if (_happiness < 20) {
        return Expression::TRISTE;
    }

    // 4. Muy aburrido → aburrido
    if (_boredom > 75) {
        return Expression::ABURRIDO;
    }

    // 5. Feliz y no aburrido → feliz
    if (_happiness > 70 && _boredom < 40) {
        return Expression::FELIZ;
    }

    // 6. Caso base
    return Expression::NEUTRAL;
}

// ─────────────────────────────────────────────────────────────
//  applyOfflineDecay(): decaimiento por tiempo apagado
//  Llamar una sola vez cuando NTP entregue hora válida.
// ─────────────────────────────────────────────────────────────

void Mood::applyOfflineDecay(time_t nowEpoch) {
    // Cargar lastEpoch desde NVS (ya lo tenemos en _lastKnownEpoch tras begin())
    if (_lastKnownEpoch <= 0) {
        // Sin referencia de tiempo anterior, no hacer nada
        Serial.println("[mood] decay offline: sin epoch previo, se omite");
        return;
    }

    if (nowEpoch <= _lastKnownEpoch) {
        // El tiempo no avanzó (o retrocedió) → ignorar
        return;
    }

    // Tiempo transcurrido apagado, con tope máximo
    time_t dt = nowEpoch - _lastKnownEpoch;
    if (dt > (time_t)OFFLINE_DECAY_TOPE_S) {
        dt = (time_t)OFFLINE_DECAY_TOPE_S;
    }

    // Convertir a minutos para aplicar las mismas tasas por minuto
    uint32_t minutos = (uint32_t)(dt / 60);

    if (minutos == 0) {
        return;
    }

    // Aplicar decaimiento acumulado (mismas tasas que el tick en update())
    // Nota: el doble de felicidad si aburrimiento > 70 se evalúa al inicio
    // del cálculo (estado actual); para simplicidad y evitar malloc/loops
    // se aplica la tasa base; el boredom subirá con el tiempo de todas formas.
    uint8_t felDecayTotal  = (uint8_t)min((uint32_t)MOOD_DECAY_FELICIDAD_PM * minutos, (uint32_t)100);
    uint8_t engDecayTotal  = (uint8_t)min((uint32_t)MOOD_DECAY_ENERGIA_PM   * minutos, (uint32_t)100);
    uint8_t borRiseTotal   = (uint8_t)min((uint32_t)MOOD_SUBE_ABURRIM_PM    * minutos, (uint32_t)100);

    _happiness = _subSat(_happiness, felDecayTotal);
    _energy    = _subSat(_energy,    engDecayTotal);
    _boredom   = _addSat(_boredom,   borRiseTotal);

    Serial.printf("[mood] decaimiento offline: %u min apagado -> F:%u E:%u A:%u\n",
                  minutos, _happiness, _energy, _boredom);

    // Actualizar epoch de referencia al tiempo actual y guardar
    _lastKnownEpoch = nowEpoch;
    _timeValid      = true;
    _saveToNVS();
}

// ─────────────────────────────────────────────────────────────
//  noteTimeValid(): registrar que ya tenemos hora válida
// ─────────────────────────────────────────────────────────────

void Mood::noteTimeValid(time_t nowEpoch) {
    _timeValid      = true;
    _lastKnownEpoch = nowEpoch;
    // No guardamos aquí para no desgastar NVS en cada llamada;
    // el próximo guardado (apply o diferido) incluirá el epoch.
}

// ─────────────────────────────────────────────────────────────
//  set(): seteo directo para comandos seriales de test
// ─────────────────────────────────────────────────────────────

void Mood::set(uint8_t h, uint8_t e, uint8_t b) {
    // Clampear a [0, 100] por si el caller pasa valores fuera de rango
    _happiness = (h > 100u) ? 100u : h;
    _energy    = (e > 100u) ? 100u : e;
    _boredom   = (b > 100u) ? 100u : b;

    Serial.printf("[mood] set() -> F:%u E:%u A:%u\n", _happiness, _energy, _boredom);

    // Guardado inmediato
    _saveToNVS();
}
