// sound.cpp — Módulo de sonido para espToy
// Buzzer pasivo en PIN_BUZZER via LEDC (API arduino-esp32 2.x).
// Si el buzzer no está conectado, el módulo funciona igual (sin sonido).

#include "sound.h"
#include "config.h"
#include <Preferences.h>

// =============================================================
//  Definición de melodías
//  Cada nota: { frecuenciaHz, duracionMs }
//  frecuencia 0 = silencio/pausa
// =============================================================

// BOOT / DESPERTAR: Do-Mi-Sol ascendente (~400 ms total)
static const Sound::Nota melodia_boot[] = {
    { 523, 130 },  // Do4
    { 659, 130 },  // Mi4
    { 784, 130 },  // Sol4
};

// FELIZ: dos bips agudos rápidos con pausa entre ellos
static const Sound::Nota melodia_feliz[] = {
    { 880,  60 },  // bip agudo
    {   0,  40 },  // pausa
    { 880,  60 },  // bip agudo
};

// TRISTE: descendente lenta 500→400→300 Hz
static const Sound::Nota melodia_triste[] = {
    { 500, 120 },
    { 400, 120 },
    { 300, 120 },
};

// ENOJADO: bip grave con segundo pulso más grave
static const Sound::Nota melodia_enojado[] = {
    { 200, 150 },  // bip grave
    {   0,  30 },  // pausa corta
    { 150, 100 },  // segundo pulso más grave
};

// AMOR: trino suave 600/700 alternado
static const Sound::Nota melodia_amor[] = {
    { 600, 70 },
    { 700, 70 },
    { 600, 70 },
    { 700, 70 },
};

// SORPRESA: bip corto agudo
static const Sound::Nota melodia_sorpresa[] = {
    { 1000, 80 },
};

// DORMIR: 4 notas descendentes ralentizándose
static const Sound::Nota melodia_dormir[] = {
    { 600, 120 },
    { 500, 160 },
    { 400, 200 },
    { 300, 260 },
};

// DESPERTAR: igual que BOOT (Do-Mi-Sol ascendente)
static const Sound::Nota melodia_despertar[] = {
    { 523, 130 },  // Do4
    { 659, 130 },  // Mi4
    { 784, 130 },  // Sol4
};

// RONQUIDO: una sola nota grave (se dispara periódicamente desde afuera)
static const Sound::Nota melodia_ronquido[] = {
    { 150, 90 },
};

// BIP: genérico corto (para Pong y usos varios)
static const Sound::Nota melodia_bip[] = {
    { 900, 25 },
};

// =============================================================
//  Tabla de despacho: Melody → puntero + cantidad de notas
// =============================================================

struct MelodiaDesc {
    const Sound::Nota* notas;
    uint8_t            total;
};

// Orden debe coincidir con enum class Melody
static const MelodiaDesc tabla[] = {
    { melodia_boot,      sizeof(melodia_boot)      / sizeof(Sound::Nota) },
    { melodia_feliz,     sizeof(melodia_feliz)     / sizeof(Sound::Nota) },
    { melodia_triste,    sizeof(melodia_triste)    / sizeof(Sound::Nota) },
    { melodia_enojado,   sizeof(melodia_enojado)   / sizeof(Sound::Nota) },
    { melodia_amor,      sizeof(melodia_amor)      / sizeof(Sound::Nota) },
    { melodia_sorpresa,  sizeof(melodia_sorpresa)  / sizeof(Sound::Nota) },
    { melodia_dormir,    sizeof(melodia_dormir)    / sizeof(Sound::Nota) },
    { melodia_despertar, sizeof(melodia_despertar) / sizeof(Sound::Nota) },
    { melodia_ronquido,  sizeof(melodia_ronquido)  / sizeof(Sound::Nota) },
    { melodia_bip,       sizeof(melodia_bip)       / sizeof(Sound::Nota) },
};

// =============================================================
//  Instancia global
// =============================================================
Sound sound;

// =============================================================
//  Implementación
// =============================================================

void Sound::begin() {
    // Cargar estado de sonido desde NVS (persiste entre reinicios)
    {
        Preferences prefs;
        if (prefs.begin("snd", /*readOnly=*/true)) {
            if (prefs.isKey("habilitado")) {
                _habilitado = prefs.getBool("habilitado", SONIDO_HABILITADO_DEFAULT);
            } else {
                _habilitado = SONIDO_HABILITADO_DEFAULT;
            }
            prefs.end();
        } else {
            _habilitado = SONIDO_HABILITADO_DEFAULT;
        }
    }

    _tocando    = false;
    _melodia    = nullptr;
    _totalNotas = 0;
    _indice     = 0;

    // Configurar canal LEDC (API 2.x: ledcSetup + ledcAttachPin)
    ledcSetup(BUZZER_LEDC_CANAL, 1000, BUZZER_LEDC_RES);
    ledcAttachPin(PIN_BUZZER, BUZZER_LEDC_CANAL);

    // Asegurarse de que empiece en silencio
    ledcWriteTone(BUZZER_LEDC_CANAL, 0);

    Serial.printf("[sound] listo (habilitado=%d)\n", _habilitado ? 1 : 0);
}

// Guardar el estado de habilitado en NVS
void Sound::_saveEnabled() {
    Preferences prefs;
    if (prefs.begin("snd", /*readOnly=*/false)) {
        prefs.putBool("habilitado", _habilitado);
        prefs.end();
    }
}

void Sound::_iniciarNota() {
    if (_melodia == nullptr || _indice >= _totalNotas) {
        // Melodía terminada
        _silenciar();
        _tocando = false;
        return;
    }

    const Nota& n = _melodia[_indice];

    if (n.hz > 0) {
        // ledcWriteTone fija el duty al 50% (máximo); lo bajamos al porcentaje
        // configurado en SOUND_VOLUMEN_PCT para reducir el volumen del buzzer.
        ledcWriteTone(BUZZER_LEDC_CANAL, n.hz);
        // Duty máximo para resolución de BUZZER_LEDC_RES bits: 2^res - 1
        uint32_t dutyMax = (1u << BUZZER_LEDC_RES) - 1u;
        uint32_t dutyBajo = dutyMax * SOUND_VOLUMEN_PCT / 100u;
        ledcWrite(BUZZER_LEDC_CANAL, dutyBajo);
    } else {
        // Pausa: silenciar sin detener el estado
        ledcWriteTone(BUZZER_LEDC_CANAL, 0);
    }

    _tiempoNota = millis();
}

void Sound::_silenciar() {
    ledcWriteTone(BUZZER_LEDC_CANAL, 0);
}

void Sound::update(uint32_t now) {
    if (!_tocando || _melodia == nullptr) return;

    // Verificar si la nota actual ya terminó
    const Nota& n = _melodia[_indice];
    if ((now - _tiempoNota) >= n.ms) {
        _indice++;
        if (_indice >= _totalNotas) {
            // Melodía completa: apagar y marcar como terminada
            _silenciar();
            _tocando    = false;
            _melodia    = nullptr;
            _totalNotas = 0;
            _indice     = 0;
        } else {
            _iniciarNota();
        }
    }
}

void Sound::play(Melody m) {
    if (!_habilitado) return;

    // Detener lo que haya en curso
    _silenciar();

    uint8_t idx = static_cast<uint8_t>(m);
    if (idx >= sizeof(tabla) / sizeof(tabla[0])) return;  // índice inválido

    _melodia    = tabla[idx].notas;
    _totalNotas = tabla[idx].total;
    _indice     = 0;
    _tocando    = true;

    _iniciarNota();
}

void Sound::stop() {
    _silenciar();
    _tocando    = false;
    _melodia    = nullptr;
    _totalNotas = 0;
    _indice     = 0;
}

void Sound::setEnabled(bool en) {
    _habilitado = en;
    if (!en) {
        stop();  // detener inmediatamente si se deshabilita
    }
    // Persistir en NVS para que el estado survive al reinicio
    _saveEnabled();
}

bool Sound::enabled() const {
    return _habilitado;
}
