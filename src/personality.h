#pragma once
#include <Arduino.h>
#include <time.h>
#include "face.h"

// =============================================================
//  personality.h — Módulo de personalidad de espToy
//  4 rasgos independientes 0-100 (alegre, grunon, energetico,
//  perezoso). Aprenden por muestreo pasivo del estado de humor
//  y por eventos de interacción (doc 06 §3).
//
//  Plasticidad §3.3: los primeros PERSONALIDAD_DIAS_FORMACION días
//  los deltas se aplican ×1.0; luego ×PERSONALIDAD_FACTOR_MADURO.
//  El factor se aplica mediante acumuladores float por rasgo para
//  que la fracción no se pierda por redondeo.
//
//  Persistencia: NVS namespace "esptoy", claves "pAlegre",
//  "pGrunon", "pEnerg", "pPerez", "pBirth".
// =============================================================

// Eventos discretos que modifican la personalidad
enum class PersEvent : uint8_t {
    CARICIA,             // caricia de día → alegre+1, energetico+1
    COSQUILLAS_OK,       // cosquillas bien recibidas → alegre+1, energetico+2
                         //   (incluye el +1 de "cualquier interacción → energetico+1")
    ENOJO_COSQUILLAS,    // demasiadas cosquillas → grunon+2, energetico+1
    ENOJO_NOCTURNO       // despertado de noche → grunon+2
};

class Personality {
public:
    // Carga rasgos de NVS o usa defaults PERSONALIDAD_INI (50)
    void begin();

    // Muestreo pasivo (doc §3.2): llamar cada tick de humor (MOOD_TICK_MS/TIME_SCALE)
    // SOLO si el dispositivo está DESPIERTO (IDLE o siesta diurna).
    // El sueño nocturno NO cuenta — el caller se encarga de no llamar de noche.
    // descansando=true activa el caso DORMIDO (siesta diurna).
    void sampleTick(Expression dominante, bool descansando);

    // Evento discreto de interacción (doc §3.2 tabla "Eventos")
    void event(PersEvent e);

    // Guardado diferido a NVS; llamar cada frame junto a mood.update()
    void update(uint32_t now);

    // Si aún no se conoce la hora de nacimiento, fijarla a nowEpoch y guardar
    void noteTimeValid(time_t nowEpoch);

    // Seteo directo de todos los rasgos para pruebas seriales; persiste en NVS
    void set(uint8_t alegre, uint8_t grunon, uint8_t energetico, uint8_t perezoso);

    // Renacer completo: rasgos al default neutro, acumuladores a 0,
    // birthEpoch = nowEpoch (0 si aún no hay hora válida). Persiste en NVS.
    void renacer(time_t nowEpoch);

    // Accessores de solo lectura
    uint8_t alegre()      const { return _alegre;     }
    uint8_t grunon()      const { return _grunon;     }
    uint8_t energetico()  const { return _energetico; }
    uint8_t perezoso()    const { return _perezoso;   }

    // Edad en días completos desde el nacimiento; -1 si no hay hora válida.
    int edadDias() const;

    // ── Helpers de modulación (doc §3.4) consultados por mood.cpp / main.cpp ──

    // Devuelve true si rasgo supera el umbral "alto" (PERSONALIDAD_UMBRAL_ALTO)
    static bool esAlta(uint8_t rasgo);

    // Máx. cosquillas seguidas antes del enojo: 2 si gruñón alto, 3 en otro caso
    uint8_t tickleMax() const;

    // Duración del malhumor en ms: base ×2 si gruñón alto; ÷2 si alegre alto;
    // si ambos altos se cancelan (base)
    uint32_t malhumorMs() const;

    // Decay de felicidad por tick: PERSONALIDAD_DECAY_FELIZ_ALEGRE si alegre alto,
    // MOOD_DECAY_FELICIDAD_PM en otro caso
    uint8_t decayFelicidad() const;

    // Decay de energía por tick: base ×2 si perezoso alto.
    // Si energetico alto (y perezoso no alto) la energía decae 1 tick sí / 1 tick no.
    // Si ambos altos → base normal.
    // mood.cpp usa energiaDecaeMitad() para la lógica alternante.
    uint8_t decayEnergia() const;

    // True cuando la energía debe decaer solo en ticks alternos (energetico alto, perezoso no alto)
    bool energiaDecaeMitad() const;

    // Puntos de recuperación de energía por tick de sueño:
    // base MOOD_RECUPERA_ENERGIA_PT +PERSONALIDAD_RECUPERA_DELTA si energetico alto,
    // −PERSONALIDAD_RECUPERA_DELTA si perezoso alto (acumulables: ambos → base)
    uint8_t recuperaEnergia() const;

    // Umbral de energía para entrar en siesta: 10 si perezoso alto, 5 si no
    uint8_t umbralSiesta() const;

    // Factor de plasticidad actual (1.0 en formación, PERSONALIDAD_FACTOR_MADURO luego)
    float plasticidadFactor() const;

private:
    // Rasgos: 0-100
    uint8_t _alegre;
    uint8_t _grunon;
    uint8_t _energetico;
    uint8_t _perezoso;

    // Epoch del primer momento con hora válida (nacimiento); 0 = desconocido
    time_t  _birthEpoch;

    // Acumuladores float para plasticidad fraccionaria (§3.3)
    float _accAlegre;
    float _accGrunon;
    float _accEnerg;
    float _accPerez;

    // Guardado diferido
    bool     _dirty;
    uint32_t _lastSaveMs;

    // Persistencia NVS
    void _saveToNVS();
    void _loadFromNVS();

    // Aplica un delta con factor de plasticidad al acumulador y transfiere al rasgo
    void _applyDelta(uint8_t& rasgo, float& acc, int8_t delta);

    // Aritmética saturada en [0, 100]
    static uint8_t _addSat(uint8_t v, uint8_t delta);
    static uint8_t _subSat(uint8_t v, uint8_t delta);
};

extern Personality personality;
