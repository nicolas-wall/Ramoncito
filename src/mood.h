#pragma once
#include <Arduino.h>
#include <time.h>
#include "face.h"

// =============================================================
//  mood.h — Cerebro Tamagotchi de espToy
//  Estado interno: happiness, energy, boredom (0-100 c/u)
//  Decaimiento temporal, efectos de interacción, persistencia NVS.
// =============================================================

// Efectos que pueden aplicarse al humor desde el exterior
enum class MoodEffect : uint8_t {
    CARICIA,             // sensor táctil (cabeza)
    COSQUILLAS,          // toque en el pie — felicidad leve
    COSQUILLAS_SEGUIDAS, // demasiadas cosquillas seguidas → se enoja
    LEVANTADO,           // lo alzan (IMU) — alegría, baja aburrimiento
    SACUDIDA_LEVE,       // sacudida suave (IMU) — estimulante, baja aburrimiento
    SACUDIDA_EXCESIVA    // sacudida excesiva (IMU) — molesto, baja felicidad
};

class Mood {
public:
    // Carga estado desde NVS o inicializa con defaults si no hay datos guardados
    void begin();

    // Decaimiento temporal + guardado diferido (llamar cada frame o cada FRAME_MS).
    // descansando=true (durmiendo de noche o siesta por agotamiento):
    // en vez de decaer, la energía se recupera de a poco.
    void update(uint32_t now, bool descansando = false);

    // Aplica un efecto de interacción y guarda inmediatamente en NVS
    void apply(MoodEffect e);

    // Selecciona la expresión dominante según tabla doc 03 §5 (orden corregido)
    Expression dominantExpression() const;

    // Aplica decaimiento por tiempo apagado; llamar cuando la hora NTP ya es válida
    void applyOfflineDecay(time_t nowEpoch);

    // Registra el epoch actual como referencia para el guardado y el decay offline
    void noteTimeValid(time_t nowEpoch);

    // Accesores de solo lectura
    uint8_t happiness() const { return _happiness; }
    uint8_t energy()    const { return _energy;    }
    uint8_t boredom()   const { return _boredom;   }

    // Seteo directo para comandos seriales de test (también persiste en NVS)
    void set(uint8_t h, uint8_t e, uint8_t b);

    // Reset completo al nacer: vuelve a los defaults y persiste en NVS
    void reset();

private:
    // Variables de estado (0-100)
    uint8_t _happiness;
    uint8_t _energy;
    uint8_t _boredom;

    // Temporización del decaimiento
    uint32_t _lastTickMs;   // millis del último tick de decaimiento

    // Temporización del guardado diferido
    uint32_t _lastSaveMs;   // millis del último guardado a NVS
    bool     _dirty;        // hay cambios sin guardar

    // Referencia de tiempo real para decay offline y guardado de epoch
    time_t   _lastKnownEpoch;   // último epoch válido conocido
    bool     _timeValid;        // si alguna vez se recibió hora válida

    // helpers internos
    void _saveToNVS();
    void _loadFromNVS();

    // Suma/resta saturando en [0, 100] sin wrap-around
    static uint8_t _addSat(uint8_t v, uint8_t delta);
    static uint8_t _subSat(uint8_t v, uint8_t delta);
};

extern Mood mood;
