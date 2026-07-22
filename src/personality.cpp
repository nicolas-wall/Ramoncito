// =============================================================
//  personality.cpp — Módulo de personalidad de Ramoncito
//  2 ejes bipolares 0-100: ÁNIMO (gruñón↔alegre) y
//  ENERGÍA (perezoso↔energético). Los 4 rasgos clásicos se
//  derivan de estos ejes (ver personality.h).
//  Aprendizaje por muestreo pasivo y eventos.
//  Plasticidad §3.3: acumuladores float para que el factor ×0.25
//  no se pierda por redondeo — cada ajuste suma delta×factor al
//  acumulador; cuando |acumulador| >= 1 se transfiere la parte
//  entera al eje (saturado 0-100).
// =============================================================

#include "personality.h"
#include "config.h"
#include <Preferences.h>
#include <math.h>

// Instancia global accesible desde main.cpp y mood.cpp
Personality personality;

// ── Namespace y claves NVS ────────────────────────────────────
static const char* PERS_NS      = "ramoncito";
static const char* PERS_ANIMO   = "pAnimo";
static const char* PERS_ENERGIA = "pEnergia";
static const char* PERS_BIRTH   = "pBirth";
// Claves viejas (4 rasgos) — solo para migración al modelo de 2 ejes
static const char* PERS_OLD_ALEGRE = "pAlegre";
static const char* PERS_OLD_ENERG  = "pEnerg";

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
//  transfiere la parte entera al eje saturado.
// ─────────────────────────────────────────────────────────────

void Personality::_applyDelta(uint8_t& rasgo, float& acc, int8_t delta) {
    float factor = plasticidadFactor();
    acc += (float)delta * factor;

    // Transferir parte entera al eje
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
        _animo      = PERSONALIDAD_INI;
        _energia    = PERSONALIDAD_INI;
        _birthEpoch = 0;
        Serial.println("[pers] NVS vacío, usando defaults");
        return;
    }

    if (prefs.isKey(PERS_ANIMO)) {
        // Modelo nuevo (2 ejes)
        _animo      = prefs.getUChar(PERS_ANIMO,   PERSONALIDAD_INI);
        _energia    = prefs.getUChar(PERS_ENERGIA, PERSONALIDAD_INI);
        _birthEpoch = (time_t)prefs.getLong64(PERS_BIRTH, 0);
        Serial.printf("[pers] cargado NVS -> animo:%u energia:%u birth:%lld\n",
                      _animo, _energia, (long long)_birthEpoch);
    } else if (prefs.isKey(PERS_OLD_ALEGRE)) {
        // Migración desde el modelo viejo (4 rasgos): animo=alegre, energia=energetico
        _animo      = prefs.getUChar(PERS_OLD_ALEGRE, PERSONALIDAD_INI);
        _energia    = prefs.getUChar(PERS_OLD_ENERG,  PERSONALIDAD_INI);
        _birthEpoch = (time_t)prefs.getLong64(PERS_BIRTH, 0);
        Serial.printf("[pers] migrado 4->2 ejes -> animo:%u energia:%u birth:%lld\n",
                      _animo, _energia, (long long)_birthEpoch);
    } else {
        _animo      = PERSONALIDAD_INI;
        _energia    = PERSONALIDAD_INI;
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
    prefs.putUChar(PERS_ANIMO,   _animo);
    prefs.putUChar(PERS_ENERGIA, _energia);
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
    _accAnimo   = 0.0f;
    _accEnergia = 0.0f;

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
        _applyDelta(_energia, _accEnergia, -2);   // dormir de día → más perezoso
    } else {
        switch (dominante) {
            case Expression::FELIZ:
                _applyDelta(_animo, _accAnimo, +1);   // hacia alegre
                break;
            case Expression::ENOJADO:
                _applyDelta(_animo, _accAnimo, -2);   // hacia gruñón
                break;
            case Expression::TRISTE:
                _applyDelta(_animo, _accAnimo, -1);   // hacia gruñón
                break;
            case Expression::ABURRIDO:
                _applyDelta(_energia, _accEnergia, -1); // hacia perezoso
                break;
            case Expression::NEUTRAL:
            default:
                // Sin cambio (doc §3.2)
                break;
        }
    }

    if (_dirty) {
        Serial.printf("[pers] sample %d -> animo:%u energia:%u f:%.2f\n",
                      (int)dominante, _animo, _energia, plasticidadFactor());
    }
}

// ─────────────────────────────────────────────────────────────
//  event(): evento discreto de interacción (doc §3.2 "Eventos")
//    CARICIA           animo+1, energia+1
//    COSQUILLAS_OK     animo+1, energia+2
//    ENOJO_COSQUILLAS  animo-2, energia+1
//    ENOJO_NOCTURNO    animo-2
//    LEVANTADO         animo+1, energia+1
// ─────────────────────────────────────────────────────────────

void Personality::event(PersEvent e) {
    const char* nombre = "?";

    switch (e) {
        case PersEvent::CARICIA:
            _applyDelta(_animo,   _accAnimo,   +1);
            _applyDelta(_energia, _accEnergia, +1);
            nombre = "CARICIA";
            break;

        case PersEvent::COSQUILLAS_OK:
            _applyDelta(_animo,   _accAnimo,   +1);
            _applyDelta(_energia, _accEnergia, +2);
            nombre = "COSQUILLAS_OK";
            break;

        case PersEvent::ENOJO_COSQUILLAS:
            _applyDelta(_animo,   _accAnimo,   -2);
            _applyDelta(_energia, _accEnergia, +1);
            nombre = "ENOJO_COSQUILLAS";
            break;

        case PersEvent::ENOJO_NOCTURNO:
            _applyDelta(_animo, _accAnimo, -2);
            nombre = "ENOJO_NOCTURNO";
            break;

        case PersEvent::LEVANTADO:
            _applyDelta(_animo,   _accAnimo,   +1);
            _applyDelta(_energia, _accEnergia, +1);
            nombre = "LEVANTADO";
            break;
    }

    Serial.printf("[pers] evento %s -> animo:%u energia:%u f:%.2f\n",
                  nombre, _animo, _energia, plasticidadFactor());
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
//  set(): seteo directo de los dos ejes para pruebas seriales
// ─────────────────────────────────────────────────────────────

void Personality::set(uint8_t animo, uint8_t energia) {
    _animo   = (animo   > 100u) ? 100u : animo;
    _energia = (energia > 100u) ? 100u : energia;

    // Resetear acumuladores para que los nuevos valores sean el punto de partida
    _accAnimo = _accEnergia = 0.0f;

    Serial.printf("[pers] set() -> animo:%u energia:%u\n", _animo, _energia);
    _saveToNVS();
}

// ─────────────────────────────────────────────────────────────
//  renacer(): reseteo completo de personalidad y nacimiento
// ─────────────────────────────────────────────────────────────

void Personality::renacer(time_t nowEpoch) {
    _animo   = PERSONALIDAD_INI;
    _energia = PERSONALIDAD_INI;

    // Resetear acumuladores float para que no queden sesgos
    _accAnimo   = 0.0f;
    _accEnergia = 0.0f;

    // nowEpoch=0 → sin hora válida todavía; quedará "s/edad" hasta NTP
    _birthEpoch = nowEpoch;

    Serial.printf("[pers] renacer -> animo:%u energia:%u birth:%lld\n",
                  _animo, _energia, (long long)_birthEpoch);

    // Persistir inmediatamente (fuerza guardado de pBirth aunque sea 0)
    Preferences prefs;
    if (prefs.begin(PERS_NS, /*readOnly=*/false)) {
        prefs.putUChar(PERS_ANIMO,   _animo);
        prefs.putUChar(PERS_ENERGIA, _energia);
        prefs.putLong64(PERS_BIRTH, (int64_t)_birthEpoch);
        prefs.end();
    }
    _dirty      = false;
    _lastSaveMs = millis();
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
//  Consultan los rasgos clásicos vía los accessores derivados.
// ─────────────────────────────────────────────────────────────

bool Personality::esAlta(uint8_t rasgo) {
    return rasgo > PERSONALIDAD_UMBRAL_ALTO;
}

uint8_t Personality::tickleMax() const {
    return esAlta(grunon()) ? TICKLE_SEGUIDAS_GRUNON : TICKLE_SEGUIDAS_MAX;
}

uint32_t Personality::malhumorMs() const {
    bool gAlto = esAlta(grunon());
    bool aAlto = esAlta(alegre());
    if (gAlto && aAlto)  return MALHUMOR_MS;           // se cancelan → base
    if (gAlto)           return MALHUMOR_MS * 2UL;
    if (aAlto)           return MALHUMOR_MS / 2UL;
    return MALHUMOR_MS;
}

uint8_t Personality::decayFelicidad() const {
    return esAlta(alegre()) ? PERSONALIDAD_DECAY_FELIZ_ALEGRE : MOOD_DECAY_FELICIDAD_PM;
}

uint8_t Personality::decayEnergia() const {
    bool eAlto = esAlta(energetico());
    bool pAlto = esAlta(perezoso());
    if (pAlto && eAlto)  return MOOD_DECAY_ENERGIA_PM;       // se cancelan → base
    if (pAlto)           return MOOD_DECAY_ENERGIA_PM * 2u;
    // Si energetico alto (y perezoso no alto): base normal —
    // la reducción a mitad se gestiona mediante energiaDecaeMitad()
    return MOOD_DECAY_ENERGIA_PM;
}

bool Personality::energiaDecaeMitad() const {
    // True cuando la energía debe decaer solo 1 tick sí / 1 tick no
    return esAlta(energetico()) && !esAlta(perezoso());
}

uint8_t Personality::recuperaEnergia() const {
    bool eAlto = esAlta(energetico());
    bool pAlto = esAlta(perezoso());
    int16_t base = (int16_t)MOOD_RECUPERA_ENERGIA_PT;
    if (eAlto) base += (int16_t)PERSONALIDAD_RECUPERA_DELTA;
    if (pAlto) base -= (int16_t)PERSONALIDAD_RECUPERA_DELTA;
    if (base < 1)   base = 1;
    if (base > 100) base = 100;
    return (uint8_t)base;
}

uint8_t Personality::umbralSiesta() const {
    return esAlta(perezoso()) ? PERSONALIDAD_UMBRAL_SIESTA_PEREZOSO
                              : PERSONALIDAD_UMBRAL_SIESTA_BASE;
}
