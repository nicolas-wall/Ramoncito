#pragma once
#include <Arduino.h>

// Melodías disponibles para el buzzer pasivo
enum class Melody : uint8_t {
    BOOT, FELIZ, TRISTE, ENOJADO, AMOR, SORPRESA,
    DORMIR, DESPERTAR, RONQUIDO, BIP
};

class Sound {
public:
    // Estructura de una nota: frecuencia en Hz (0 = silencio/pausa) y duración en ms
    // (pública: las tablas de melodías de sound.cpp viven a nivel de archivo)
    struct Nota {
        uint16_t hz;
        uint16_t ms;
    };

    void begin();
    void update(uint32_t now);   // avanza la melodía en curso, no bloquea
    void play(Melody m);         // corta lo que suene y arranca esta
    void stop();
    void setEnabled(bool en);
    bool enabled() const;

private:

    // Melodía activa
    const Nota* _melodia     = nullptr;
    uint8_t     _totalNotas  = 0;
    uint8_t     _indice      = 0;

    // Control de tiempo
    uint32_t _tiempoNota     = 0;   // millis() cuando empezó la nota actual

    // Estado general
    bool _habilitado         = true;
    bool _tocando            = false;

    // Métodos internos
    void _iniciarNota();
    void _silenciar();
};

// Instancia global
extern Sound sound;
