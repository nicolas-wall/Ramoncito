// =============================================================
//  personality.cpp — Módulo de personalidad de espToy
//  4 rasgos independientes 0-100 (alegre, grunon, energetico,
//  perezoso). Aprendizaje por muestreo pasivo y eventos.
//  Plasticidad §3.3: acumuladores float para que el factor ×0.25
//  no se pierda por redondeo — cada ajuste suma delta×factor al
//  acumulador; cuando |acumulador| >= 1 se transfiere la parte
//  entera al rasgo (saturado 0-100).
// =============================================================

#include "personality.h"
#include "config.h"
#include <Preferences.h>
#include <math.h>

// Instancia global accesible desde main.cpp y mood.cpp
Personality personality;

// ── Namespace y claves NVS ────────────────────────────────────
static const char* PERS_NS     = "esptoy";
static const char* PERS_ALEGRE = "pAlegre";
static const char* PERS_GRUNON = "pGrunon";
static const char* PERS_ENERG  = "pEnerg";
static const char* PERS_PEREZ  = "pPerez";
static const char* PERS_BIRTH  = "pBirth";

// ─────────────────────────────────────────────────────────────
//  Helpers: aritmética saturada en [0, 100]
// ─────────────────────────────────────────────────────────────

uint8_t Personality::_addSat(uint8_t v, uint8_t delta) {
    uint16_t r = (uint16_t)v + delta;
    return (r > 100u) ? 100u : (uint8_t)r;
}

uint8_t Personality::_subSat(uint8_t v, uint8_t delta) {
    return (delta >= v) ? 0u : (v - delta);
}

// ─────────────────────────────────────────────────────────────
//  _applyDelta(): aplica un delta con plasticidad via acumulador.
//  El acumulador recibe delta×factor; cuando |acc| >= 1 se
//  transfiere la parte entera al rasgo saturado.
// ─────────────────────────────────────────────────────────────

void Personality::_applyDelta(uint8_t& rasgo, float& acc, int8_t delta) {
    float factor = plasticidadFactor();
    acc += (float)delta * factor;

    // Transferir parte entera al rasgo
    if (acc >= 1.0f) {
        int8_t entero = (int8_t)acc;
        rasgo = _addSat(rasgo, (uint8_t)entero);
        acc  -= (float)entero;
        _dirty = true;
    } else if (acc <= -1.0f) {
        int8_t entero = (int8_t)(-acc);  // positivo
        rasgo = _subSat(rasgo, (uint8_t)entero);
        acc  += (float)entero;
        _dirty = true;
    }
}

// ─────────────────────────────────────────────────────────────
//  Persistencia NVS
// ─────────────────────────────────────────────────────────────

void Personality::_loadFromNVS() {
    Preferences prefs;
    if (!prefs.begin(PERS_NS, /*readOnly=*/true)) {
        // Namespace aún no existe: defaults
        _alegre     = PERSONALIDAD_INI;
        _grunon     = PERSONALIDAD_INI;
        _energetico = PERSONALIDAD_INI;
        _perezoso   = PERSONALIDAD_INI;
        _birthEpoch = 0;
        Serial.println("[pers] NVS vacío, usando defaults");
        return;
    }

    if (prefs.isKey(PERS_ALEGRE)) {
        _alegre     = prefs.getUChar(PERS_ALEGRE, PERSONALIDAD_INI);
        _grunon     = prefs.getUChar(PERS_GRUNON, PERSONALIDAD_INI);
        _energetico = prefs.getUChar(PERS_ENERG,  PERSONALIDAD_INI);
        _perezoso   = prefs.getUChar(PERS_PEREZ,  PERSONALIDAD_INI);
        _birthEpoch = (time_t)prefs.getLong64(PERS_BIRTH, 0);
        Serial.printf("[pers] cargado NVS -> A:%u G:%u E:%u P:%u birth:%lld\n",
                      _alegre, _grunon, _energetico, _perezoso, (long long)_birthEpoch);
    } else {
        _alegre     = PERSONALIDAD_INI;
        _grunon     = PERSONALIDAD_INI;
        _energetico = PERSONALIDAD_INI;
        _perezoso   = PERSONALIDAD_INI;
        _birthEpoch = 0;
        Serial.println("[pers] sin datos NVS, usando defaults");
    }
    prefs.end();
}

void Personality::_saveToNVS() {
    Preferences prefs;
    if (!prefs.begin(PERS_NS, /*readOnly=*/false)) {
        Serial.println("[pers] ERROR: no se pudo abrir NVS para escritura");
        return;
    }
    prefs.putUChar(PERS_ALEGRE, _alegre);
    prefs.putUChar(PERS_GRUNON, _grunon);
    prefs.putUChar(PERS_ENERG,  _energetico);
    prefs.putUChar(PERS_PEREZ,  _perezoso);
    if (_birthEpoch != 0) {
        prefs.putLong64(PERS_BIRTH, (int64_t)_birthEpoch);
    }
    prefs.end();
    _dirty      = false;
    _lastSaveMs = millis();
}

// ─────────────────────────────────────────────────────────────
//  begin(): inicialización desde NVS o defaults
// ─────────────────────────────────────────────────────────────

void Personality::begin() {
    _dirty      = false;
    _lastSaveMs = millis();

    // Acumuladores float para plasticidad fraccionaria
    _accAlegre = 0.0f;
    _accGrunon = 0.0f;
    _accEnerg  = 0.0f;
    _accPerez  = 0.0f;

    _loadFromNVS();
}

// ─────────────────────────────────────────────────────────────
//  sampleTick(): muestreo pasivo (doc §3.2 tabla "Muestreo pasivo")
//  Llamar cada tick de humor SOLO cuando el dispositivo está
//  despierto (IDLE o siesta diurna). El sueño nocturno NO llama
//  a este método — el caller se encarga de esa condición.
//  descansando=true activa el caso DORMIDO (siesta diurna).
// ─────────────────────────────────────────────────────────────

void Personality::sampleTick(Expression dominante, bool descansando) {
    // El caso DORMIDO solo aplica si descansando==true (siesta diurna)
    if (descansando && dominante == Expression::DORMIDO) {
        _applyDelta(_perezoso,   _accPerez,  +2);
        _applyDelta(_energetico, _accEnerg,  -1);
    } else {
        switch (dominante) {
            case Expression::FELIZ:
                _applyDelta(_alegre, _accAlegre, +1);
                _applyDelta(_grunon, _accGrunon, -1);
                break;
            case Expression::ENOJADO:
                _applyDelta(_grunon, _accGrunon, +2);
                _applyDelta(_alegre, _accAlegre, -1);
                break;
            case Expression::TRISTE:
                _applyDelta(_grunon, _accGrunon, +1);
                _applyDelta(_alegre, _accAlegre, -1);
                break;
            case Expression::ABURRIDO:
                _applyDelta(_perezoso, _accPerez, +1);
                break;
            case Expression::NEUTRAL:
            default:
                // Sin cambio (doc §3.2)
                break;
        }
    }

    if (_dirty) {
        Serial.printf("[pers] sample %d -> A:%u G:%u E:%u P:%u f:%.2f\n",
                      (int)dominante,
                      _alegre, _grunon, _energetico, _perezoso,
                      plasticidadFactor());
    }
}

// ─────────────────────────────────────────────────────────────
//  event(): evento discreto de interacción (doc §3.2 "Eventos")
//  Los deltas ya incluyen el +1 de "cualquier interacción →
//  energetico+1" donde aplica:
//    CARICIA           alegre+1, energetico+1
//    COSQUILLAS_OK     alegre+1, energetico+2 (= +1 interacción + +1 cosquilla)
//    ENOJO_COSQUILLAS  grunon+2, energetico+1
//    ENOJO_NOCTURNO    grunon+2 (el nocturno no cuenta como interacción energetica)
// ─────────────────────────────────────────────────────────────

void Personality::event(PersEvent e) {
    const char* nombre = "?";

    switch (e) {
        case PersEvent::CARICIA:
            _applyDelta(_alegre,     _accAlegre, +1);
            _applyDelta(_energetico, _accEnerg,  +1);
            nombre = "CARICIA";
            break;

        case PersEvent::COSQUILLAS_OK:
            _applyDelta(_alegre,     _accAlegre, +1);
            _applyDelta(_energetico, _accEnerg,  +2);
            nombre = "COSQUILLAS_OK";
            break;

        case PersEvent::ENOJO_COSQUILLAS:
            _applyDelta(_grunon,     _accGrunon, +2);
            _applyDelta(_energetico, _accEnerg,  +1);
            nombre = "ENOJO_COSQUILLAS";
            break;

        case PersEvent::ENOJO_NOCTURNO:
            _applyDelta(_grunon, _accGrunon, +2);
            nombre = "ENOJO_NOCTURNO";
            break;
    }

    Serial.printf("[pers] evento %s -> A:%u G:%u E:%u P:%u f:%.2f\n",
                  nombre, _alegre, _grunon, _energetico, _perezoso,
                  plasticidadFactor());
}

// ─────────────────────────────────────────────────────────────
//  update(): guardado diferido a NVS
// ─────────────────────────────────────────────────────────────

void Personality::update(uint32_t now) {
    if (_dirty && (now - _lastSaveMs) >= PERSONALIDAD_GUARDAR_CADA_MS) {
        _saveToNVS();
    }
}

// ─────────────────────────────────────────────────────────────
//  noteTimeValid(): registra el momento de nacimiento si aún
//  no se conoce; guarda inmediatamente en NVS.
// ─────────────────────────────────────────────────────────────

void Personality::noteTimeValid(time_t nowEpoch) {
    if (_birthEpoch == 0) {
        _birthEpoch = nowEpoch;
        _dirty      = true;
        _saveToNVS();
        Serial.printf("[pers] nacimiento registrado: %lld\n", (long long)_birthEpoch);
    }
}

// ─────────────────────────────────────────────────────────────
//  set(): seteo directo para pruebas seriales
// ─────────────────────────────────────────────────────────────

void Personality::set(uint8_t alegre, uint8_t grunon, uint8_t energetico, uint8_t perezoso) {
    _alegre     = (alegre     > 100u) ? 100u : alegre;
    _grunon     = (grunon     > 100u) ? 100u : grunon;
    _energetico = (energetico > 100u) ? 100u : energetico;
    _perezoso   = (perezoso   > 100u) ? 100u : perezoso;

    // Resetear acumuladores para que los nuevos valores sean el punto de partida
    _accAlegre = _accGrunon = _accEnerg = _accPerez = 0.0f;

    Serial.printf("[pers] set() -> A:%u G:%u E:%u P:%u\n",
                  _alegre, _grunon, _energetico, _perezoso);
    _saveToNVS();
}

// ─────────────────────────────────────────────────────────────
//  edadDias(): días completos desde el nacimiento
// ─────────────────────────────────────────────────────────────

int Personality::edadDias() const {
    if (_birthEpoch == 0) return -1;
    time_t ahora = time(nullptr);
    if (ahora <= 0) return -1;  // sin hora del sistema
    time_t diff = ahora - _birthEpoch;
    if (diff < 0) return 0;
    return (int)(diff / (time_t)86400);
}

// ─────────────────────────────────────────────────────────────
//  plasticidadFactor(): factor de aprendizaje §3.3
// ─────────────────────────────────────────────────────────────

float Personality::plasticidadFactor() const {
    int dias = edadDias();
    if (dias < 0 || dias < PERSONALIDAD_DIAS_FORMACION) {
        return 1.0f;  // en formación o sin hora válida
    }
    return PERSONALIDAD_FACTOR_MADURO;
}

// ─────────────────────────────────────────────────────────────
//  Helpers de modulación (doc §3.4)
// ─────────────────────────────────────────────────────────────

bool Personality::esAlta(uint8_t rasgo) {
    return rasgo > PERSONALIDAD_UMBRAL_ALTO;
}

uint8_t Personality::tickleMax() const {
    return esAlta(_grunon) ? TICKLE_SEGUIDAS_GRUNON : TICKLE_SEGUIDAS_MAX;
}

uint32_t Personality::malhumorMs() const {
    bool gAlto = esAlta(_grunon);
    bool aAlto = esAlta(_alegre);
    if (gAlto && aAlto)  return MALHUMOR_MS;           // se cancelan → base
    if (gAlto)           return MALHUMOR_MS * 2UL;
    if (aAlto)           return MALHUMOR_MS / 2UL;
    return MALHUMOR_MS;
}

uint8_t Personality::decayFelicidad() const {
    return esAlta(_alegre) ? PERSONALIDAD_DECAY_FELIZ_ALEGRE : MOOD_DECAY_FELICIDAD_PM;
}

uint8_t Personality::decayEnergia() const {
    bool eAlto = esAlta(_energetico);
    bool pAlto = esAlta(_perezoso);
    if (pAlto && eAlto)  return MOOD_DECAY_ENERGIA_PM;       // se cancelan → base
    if (pAlto)           return MOOD_DECAY_ENERGIA_PM * 2u;
    // Si energetico alto (y perezoso no alto): base normal —
    // la reducción a mitad se gestiona mediante energiaDecaeMitad()
    return MOOD_DECAY_ENERGIA_PM;
}

bool Personality::energiaDecaeMitad() const {
    // True cuando la energía debe decaer solo 1 tick sí / 1 tick no
    return esAlta(_energetico) && !esAlta(_perezoso);
}

uint8_t Personality::recuperaEnergia() const {
    bool eAlto = esAlta(_energetico);
    bool pAlto = esAlta(_perezoso);
    int16_t base = (int16_t)MOOD_RECUPERA_ENERGIA_PT;
    if (eAlto) base += (int16_t)PERSONALIDAD_RECUPERA_DELTA;
    if (pAlto) base -= (int16_t)PERSONALIDAD_RECUPERA_DELTA;
    if (base < 1)   base = 1;
    if (base > 100) base = 100;
    return (uint8_t)base;
}

uint8_t Personality::umbralSiesta() const {
    return esAlta(_perezoso) ? PERSONALIDAD_UMBRAL_SIESTA_PEREZOSO
                             : PERSONALIDAD_UMBRAL_SIESTA_BASE;
}
