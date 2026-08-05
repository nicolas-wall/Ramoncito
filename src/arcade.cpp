// ============================================================
//  arcade.cpp — Carrusel de juegos, pausa, fin de partida y récords
// ============================================================
#include "arcade.h"
#include "config.h"
#include "sound.h"
#include "iconos.h"
#include <Preferences.h>

#include "pong.h"
#include "snake.h"
#include "breakout.h"
#include "invaders.h"
#include "tetris.h"
#include "laberinto.h"

Arcade arcade;

// ============================================================
//  Registro de juegos
//
//  Agregar un juego nuevo es: escribir su clase implementando Juego, un
//  ícono en tools/gen-iconos.py, y una fila en esta tabla. El carrusel, el
//  deslizamiento, los puntitos de posición, la pausa, la pantalla de fin y
//  el récord salen solos.
//
//  SALIR vive acá como una tarjeta más (con juego = nullptr) en vez de ser
//  una opción aparte: así el carrusel es la única forma de navegar y no hay
//  dos convenciones conviviendo. El botón A también sale, para el que ya
//  lo sabe.
//
//  `clave` es el nombre bajo el que se guarda el récord en NVS. Es fijo y
//  corto a propósito: si dependiera del nombre visible, renombrar un juego
//  borraría el récord de todos.
// ============================================================
struct JuegoDef {
    const char*    nombre;
    const char*    clave;
    const uint8_t* icono;
    Juego*         juego;   // nullptr = tarjeta de acción (SALIR)
};

static const JuegoDef JUEGOS[] = {
    { "PONG",      "pong", ICONO_PONG,      &juegoPong      },
    { "SNAKE",     "snak", ICONO_SNAKE,     &juegoSnake     },
    { "BREAKOUT",  "brko", ICONO_BREAKOUT,  &juegoBreakout  },
    { "INVADERS",  "invd", ICONO_INVADERS,  &juegoInvaders  },
    { "TETRIS",    "tetr", ICONO_TETRIS,    &juegoTetris    },
    { "LABERINTO", "labe", ICONO_LABERINTO, &juegoLaberinto },
    { "SALIR",     "",     ICONO_SALIR,     nullptr         },
};
static const uint8_t JUEGOS_N = sizeof(JUEGOS) / sizeof(JUEGOS[0]);

static const char* NVS_NS = "arcade";

// ============================================================
//  Ciclo de vida
// ============================================================

void Arcade::begin() {
    _estado = ArcadeState::MENU;
    _salir  = false;
    _sel    = 0;
    _juego  = nullptr;
    _cargarRecords();
}

void Arcade::enter(uint32_t now) {
    _estado     = ArcadeState::MENU;
    _salir      = false;
    _sel        = 0;
    _slideDesde = 0;   // sin animación al entrar: la primera tarjeta ya está
    _juego      = nullptr;
    _tocarActividad(now);
    sound.play(Melody::BIP);
    Serial.println("[arcade] menu de juegos");
}

void Arcade::_tocarActividad(uint32_t now) {
    _timeout = now + ARCADE_TIMEOUT_MS;
}

// Mueve el carrusel una tarjeta y arranca el deslizamiento. La lista da la
// vuelta en los extremos: con pocas tarjetas, chocarse contra un tope se
// siente roto — y el botón B, único control de navegación mientras no haya
// palanca, solo avanza en un sentido.
void Arcade::_mover(int8_t dir, uint32_t now) {
    _slidePrev  = _sel;
    _sel        = (uint8_t)((_sel + JUEGOS_N + dir) % JUEGOS_N);
    _slideDir   = dir;
    _slideDesde = now;
    sound.play(Melody::BIP);
}

// ============================================================
//  Récords en NVS
// ============================================================

// Una sola apertura de NVS para todos los juegos, al arrancar el firmware.
//
// Se abre en modo lectura-escritura y no solo lectura: en un aparato recién
// flasheado el namespace todavía no existe, y abrirlo de solo lectura falla
// con un NOT_FOUND que el core imprime como error en el log de arranque.
// Abrir RW lo crea la primera vez y nunca más; no escribe nada por sí solo.
void Arcade::_cargarRecords() {
    Preferences p;
    if (!p.begin(NVS_NS, false)) return;
    for (uint8_t i = 0; i < JUEGOS_N && i < MAX_JUEGOS; i++) {
        if (JUEGOS[i].clave[0] == '\0') continue;
        _records[i] = p.getUShort(JUEGOS[i].clave, 0);
    }
    p.end();
}

void Arcade::_guardarRecord(uint8_t idx, uint16_t valor) {
    if (JUEGOS[idx].clave[0] == '\0') return;
    if (idx < MAX_JUEGOS) _records[idx] = valor;
    Preferences p;
    if (!p.begin(NVS_NS, false)) return;
    p.putUShort(JUEGOS[idx].clave, valor);
    p.end();
}

// ============================================================
//  Arranque y fin de partida
// ============================================================

void Arcade::_activar(uint32_t now) {
    Juego* j = JUEGOS[_sel].juego;
    if (j == nullptr) {            // tarjeta SALIR
        _salir = true;
        sound.play(Melody::BIP);
        return;
    }
    _juego = j;
    _juego->begin(now);
    _nuevoRecord = false;
    _estado = ArcadeState::JUGANDO;
    sound.play(Melody::DESPERTAR);
    Serial.printf("[arcade] %s — partida nueva\n", JUEGOS[_sel].nombre);
}

void Arcade::_terminarPartida() {
    _estado = ArcadeState::FIN;
    uint16_t pts = _juego->puntaje();

    // Solo se guarda si el juego dice que su puntaje sirve como récord, y
    // solo cuando mejora: escribir NVS en cada partida desgastaría la flash
    // sin ganar nada.
    if (_juego->tienePuntaje() && _sel < MAX_JUEGOS && pts > _records[_sel]) {
        _nuevoRecord = true;
        _guardarRecord(_sel, pts);
    }
    sound.play(_nuevoRecord ? Melody::AMOR : Melody::ENOJADO);
    Serial.printf("[arcade] fin de %s — %s, %u puntos%s\n",
                  JUEGOS[_sel].nombre, _juego->resultado(), pts,
                  _nuevoRecord ? " (RECORD)" : "");
}

// ============================================================
//  Eventos discretos
// ============================================================

void Arcade::handleEvent(InputEvent ev, uint32_t now) {
    _tocarActividad(now);

    // Botón A = "atrás" en todos los estados. Es la única tecla que hace
    // falta recordar para no quedarse trabado en ningún lado.
    bool atras = (ev == InputEvent::BTN_A_PRESS);
    bool ok    = (ev == InputEvent::BTN_C_PRESS || ev == InputEvent::JOY_SW_PRESS);

    switch (_estado) {

        case ArcadeState::MENU: {
            // El carrusel es horizontal: se navega con izquierda/derecha.
            bool anterior  = (ev == InputEvent::JOY_LEFT);
            bool siguiente = (ev == InputEvent::JOY_RIGHT || ev == InputEvent::BTN_B_PRESS);

            if (atras)          { _salir = true; sound.play(Melody::BIP); }
            else if (anterior)  _mover(-1, now);
            else if (siguiente) _mover(+1, now);
            else if (ok)        _activar(now);
            break;
        }

        case ArcadeState::JUGANDO:
            // A pausa; todo lo demás se lo queda el juego. Los juegos que
            // usan control continuo (paletas) leen `input` en su update y
            // simplemente ignoran esto.
            if (atras) {
                _estado = ArcadeState::PAUSA;
                sound.play(Melody::BIP);
            } else if (_juego) {
                _juego->evento(ev, now);
            }
            break;

        case ArcadeState::PAUSA:
            if (atras) {
                _estado = ArcadeState::MENU;
                _juego  = nullptr;
                sound.play(Melody::BIP);
            } else if (ok) {
                _estado = ArcadeState::JUGANDO;
                sound.play(Melody::BIP);
            }
            break;

        case ArcadeState::FIN:
            if (ok) {
                _juego->begin(now);
                _nuevoRecord = false;
                _estado = ArcadeState::JUGANDO;
                sound.play(Melody::DESPERTAR);
                Serial.printf("[arcade] %s — revancha\n", JUEGOS[_sel].nombre);
            } else if (atras) {
                _estado = ArcadeState::MENU;
                _juego  = nullptr;
                sound.play(Melody::BIP);
            }
            break;
    }
}

// ============================================================
//  update()
// ============================================================

void Arcade::update(uint32_t now) {
    if (_estado == ArcadeState::JUGANDO && _juego) {
        _juego->update(now);
        if (_juego->terminado()) _terminarPartida();
        _tocarActividad(now);   // jugando nunca se considera inactividad
        return;
    }

    // Fin del deslizamiento del carrusel. Vive acá y no en render() para que
    // la animación termine aunque algún frame no llegue a dibujarse.
    if (_slideDesde != 0 && (now - _slideDesde) >= ARCADE_SLIDE_MS) {
        _slideDesde = 0;
    }

    // En menú, pausa y fin: si nadie toca nada, el arcade se cierra solo.
    if ((int32_t)(now - _timeout) >= 0) {
        _salir = true;
        Serial.println("[arcade] cerrado por inactividad");
    }
}

// ============================================================
//  Render
// ============================================================

void Arcade::render(U8G2& u8) {
    switch (_estado) {
        case ArcadeState::MENU:    _renderMenu(u8);  break;
        case ArcadeState::JUGANDO: if (_juego) _juego->render(u8); break;
        case ArcadeState::PAUSA:   _renderPausa(u8); break;
        case ArcadeState::FIN:     _renderFin(u8);   break;
    }
}

// Una tarjeta completa —ícono arriba, nombre abajo— desplazada dx píxeles.
// El desplazamiento es lo que permite dibujar dos a la vez durante el
// deslizamiento; con dx=0 queda centrada.
void Arcade::_renderTarjeta(U8G2& u8, uint8_t idx, int16_t dx) {
    if (dx <= -128 || dx >= 128) return;   // del todo fuera de pantalla

    const JuegoDef& j = JUEGOS[idx];
    u8.drawXBMP(dx + (128 - ICONO_W) / 2, 4, ICONO_W, ICONO_H, j.icono);

    u8.setFont(u8g2_font_7x13B_tf);
    int16_t tw = u8.getStrWidth(j.nombre);
    u8.drawStr(dx + (128 - tw) / 2, 50, j.nombre);

    // Récord bajo el nombre, si el juego lleva uno y ya se jugó alguna vez.
    if (j.juego && j.juego->tienePuntaje()) {
        uint16_t r = (idx < MAX_JUEGOS) ? _records[idx] : 0;
        if (r > 0) {
            char buf[20];
            snprintf(buf, sizeof(buf), "record %u", r);
            u8.setFont(u8g2_font_4x6_tf);
            u8.drawStr(dx + (128 - u8.getStrWidth(buf)) / 2, 57, buf);
        }
    }
}

void Arcade::_renderMenu(U8G2& u8) {
    // ── Tarjetas ─────────────────────────────────────────────
    if (_slideDesde != 0) {
        // El corte de la animación lo hace update(); acá solo se lee.
        float p = (float)(millis() - _slideDesde) / (float)ARCADE_SLIDE_MS;
        if (p > 1.0f) p = 1.0f;
        // Smoothstep: arranca y frena suave. Con avance lineal el
        // deslizamiento se ve mecánico, sobre todo al pasar varias seguidas.
        p = p * p * (3.0f - 2.0f * p);
        int16_t desp = (int16_t)(128.0f * p);
        _renderTarjeta(u8, _slidePrev, (int16_t)(-_slideDir * desp));
        _renderTarjeta(u8, _sel,       (int16_t)(_slideDir * (128 - desp)));
    } else {
        _renderTarjeta(u8, _sel, 0);
    }

    // ── Marco fijo: flechas y puntitos ───────────────────────
    // Se dibujan después de las tarjetas para que queden por encima
    // mientras algo está deslizándose.
    if (JUEGOS_N > 1) {
        for (uint8_t k = 0; k < 4; k++) {
            u8.drawVLine(3 + k,   20 - k, 1 + 2 * k);   // ‹
            u8.drawVLine(124 - k, 20 - k, 1 + 2 * k);   // ›
        }
        const int8_t paso = 6;
        int16_t x0 = 64 - (JUEGOS_N * paso) / 2 + 1;
        for (uint8_t i = 0; i < JUEGOS_N; i++) {
            int16_t cx = x0 + i * paso;
            if (i == _sel) u8.drawDisc(cx, 62, 2);
            else           u8.drawPixel(cx, 62);
        }
    }
}

void Arcade::_renderPausa(U8G2& u8) {
    if (_juego) _juego->render(u8);
    // Cartel sobre el juego, con fondo sólido para que se lea encima
    u8.setDrawColor(0);
    u8.drawBox(24, 22, 80, 24);
    u8.setDrawColor(1);
    u8.drawFrame(24, 22, 80, 24);
    u8.setFont(u8g2_font_6x12_tf);
    const char* t = "PAUSA";
    u8.drawStr((128 - u8.getStrWidth(t)) / 2, 34, t);
    u8.setFont(u8g2_font_4x6_tf);
    const char* s = "C sigue  A menu";
    u8.drawStr((128 - u8.getStrWidth(s)) / 2, 43, s);
}

void Arcade::_renderFin(U8G2& u8) {
    u8.setFont(u8g2_font_7x13B_tf);
    const char* t = _juego ? _juego->resultado() : "FIN";
    u8.drawStr((128 - u8.getStrWidth(t)) / 2, 20, t);

    char buf[24];
    u8.setFont(u8g2_font_6x12_tf);
    if (_juego && _juego->tienePuntaje()) {
        snprintf(buf, sizeof(buf), "%u", _juego->puntaje());
        u8.drawStr((128 - u8.getStrWidth(buf)) / 2, 36, buf);

        u8.setFont(u8g2_font_4x6_tf);
        uint16_t rec = (_sel < MAX_JUEGOS) ? _records[_sel] : 0;
        if (_nuevoRecord) snprintf(buf, sizeof(buf), "NUEVO RECORD!");
        else              snprintf(buf, sizeof(buf), "record %u", rec);
        u8.drawStr((128 - u8.getStrWidth(buf)) / 2, 45, buf);
    }

    u8.setFont(u8g2_font_5x7_tf);
    const char* s = "C revancha   A menu";
    u8.drawStr((128 - u8.getStrWidth(s)) / 2, 60, s);
}
