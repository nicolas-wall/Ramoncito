// ============================================================
//  arcade.cpp — Mini arcade: menú de juegos + Pong
//
//  La física usa float y se actualiza una vez por frame (FRAME_MS).
//  No se normaliza por delta de tiempo a propósito: el loop mantiene
//  30 fps estables, y a esta escala un integrador de paso fijo es más
//  predecible que uno variable —un frame largo no teletransporta la
//  pelota a través de una paleta—.
// ============================================================
#include "arcade.h"
#include "config.h"
#include "sound.h"
#include <math.h>

Arcade arcade;

// Ítems del menú de juegos. El orden define los índices de _sel.
static const char* const ITEMS[ARCADE_ITEMS_N] = { "PONG", "SALIR" };
static const uint8_t ITEM_PONG  = 0;
static const uint8_t ITEM_SALIR = 1;

// ============================================================
//  Ciclo de vida
// ============================================================

void Arcade::begin() {
    _estado = ArcadeState::MENU;
    _salir  = false;
    _sel    = 0;
}

void Arcade::enter(uint32_t now) {
    _estado = ArcadeState::MENU;
    _salir  = false;
    _sel    = ITEM_PONG;
    _tocarActividad(now);
    sound.play(Melody::BIP);
    Serial.println("[arcade] entrando al menu de juegos");
}

void Arcade::_tocarActividad(uint32_t now) {
    _timeout = now + ARCADE_TIMEOUT_MS;
}

// ============================================================
//  Eventos discretos (navegación)
// ============================================================

void Arcade::handleEvent(InputEvent ev, uint32_t now) {
    _tocarActividad(now);

    // Botón A = "atrás" en todos los estados. Es la única tecla que hace
    // falta recordar para no quedarse trabado en ningún lado.
    bool atras = (ev == InputEvent::BTN_A_PRESS);
    bool ok    = (ev == InputEvent::BTN_C_PRESS || ev == InputEvent::JOY_SW_PRESS);
    bool arriba = (ev == InputEvent::JOY_UP);
    bool abajo  = (ev == InputEvent::JOY_DOWN || ev == InputEvent::BTN_B_PRESS);

    switch (_estado) {

        case ArcadeState::MENU:
            if (atras) {
                _salir = true;
                sound.play(Melody::BIP);
            } else if (arriba) {
                _sel = (uint8_t)((_sel + ARCADE_ITEMS_N - 1) % ARCADE_ITEMS_N);
                sound.play(Melody::BIP);
            } else if (abajo) {
                _sel = (uint8_t)((_sel + 1) % ARCADE_ITEMS_N);
                sound.play(Melody::BIP);
            } else if (ok) {
                if (_sel == ITEM_SALIR) {
                    _salir = true;
                    sound.play(Melody::BIP);
                } else {
                    _resetPartida(now);
                    _estado = ArcadeState::JUGANDO;
                    sound.play(Melody::DESPERTAR);
                    Serial.println("[arcade] PONG — partida nueva");
                }
            }
            break;

        case ArcadeState::JUGANDO:
            // Durante el juego, B y C mueven la paleta (se leen como estado
            // continuo en _updatePong), así que acá solo interesa la pausa.
            if (atras) {
                _estado = ArcadeState::PAUSA;
                sound.play(Melody::BIP);
            }
            break;

        case ArcadeState::PAUSA:
            if (atras) {
                _estado = ArcadeState::MENU;
                sound.play(Melody::BIP);
            } else if (ok) {
                _estado = ArcadeState::JUGANDO;
                sound.play(Melody::BIP);
            }
            break;

        case ArcadeState::FIN:
            if (ok) {
                _resetPartida(now);
                _estado = ArcadeState::JUGANDO;
                sound.play(Melody::DESPERTAR);
                Serial.println("[arcade] PONG — revancha");
            } else if (atras) {
                _estado = ArcadeState::MENU;
                sound.play(Melody::BIP);
            }
            break;
    }
}

// ============================================================
//  Pong — preparación
// ============================================================

void Arcade::_resetPartida(uint32_t now) {
    _p.ptsJug = 0;
    _p.ptsCpu = 0;
    _p.palJug = (PONG_TOP_Y + PONG_BOT_Y) / 2.0f - PONG_PALETA_H / 2.0f;
    _p.palCpu = _p.palJug;
    _p.ganoJug = false;
    _sacar(now, true);
}

void Arcade::_sacar(uint32_t now, bool haciaJug) {
    // La pelota nace en el centro y queda congelada hasta que vence la
    // pausa de saque: da tiempo a reubicar la paleta después de un punto.
    _p.bolaX = 128 / 2.0f - PONG_BOLA_LADO / 2.0f;
    _p.bolaY = (PONG_TOP_Y + PONG_BOT_Y) / 2.0f - PONG_BOLA_LADO / 2.0f;
    _p.velX  = haciaJug ? -PONG_BOLA_VEL_INI : PONG_BOLA_VEL_INI;
    // Componente vertical aleatoria pero nunca cero: un saque horizontal
    // puro se vuelve un peloteo idéntico e infinito.
    float vy = (float)(esp_random() % 100) / 100.0f - 0.5f;   // -0.5 .. +0.5
    if (fabsf(vy) < 0.15f) vy = (vy < 0) ? -0.15f : 0.15f;
    _p.velY = vy * PONG_BOLA_VEL_INI;
    _p.saqueHasta    = now + PONG_SAQUE_MS;
    _p.saqueHaciaJug = haciaJug;
}

// ============================================================
//  Pong — física
// ============================================================

void Arcade::_updatePong(uint32_t now) {
    // ── Paleta del jugador ───────────────────────────────────
    // El eje analógico y los botones se suman: así anda con la palanca,
    // con B/C, y con el stub por serial, sin ramas separadas.
    float dy = input.axisY() * PONG_PALETA_VEL;
    if (input.btnB()) dy -= PONG_PALETA_VEL;
    if (input.btnC()) dy += PONG_PALETA_VEL;

    _p.palJug += dy;
    if (_p.palJug < PONG_TOP_Y) _p.palJug = PONG_TOP_Y;
    if (_p.palJug > PONG_BOT_Y - PONG_PALETA_H) _p.palJug = PONG_BOT_Y - PONG_PALETA_H;

    // ── Paleta de la CPU ─────────────────────────────────────
    // Persigue la pelota solo cuando viene hacia ella; si va para el otro
    // lado vuelve al centro. Eso la hace parecer que "espera" en vez de
    // copiar la trayectoria, y de paso le da al jugador su ventana.
    float objetivoCpu;
    if (_p.velX > 0) objetivoCpu = _p.bolaY + PONG_BOLA_LADO / 2.0f;
    else             objetivoCpu = (PONG_TOP_Y + PONG_BOT_Y) / 2.0f;

    float centroCpu = _p.palCpu + PONG_PALETA_H / 2.0f;
    float difCpu    = objetivoCpu - centroCpu;
    if (fabsf(difCpu) > PONG_CPU_ZONA_MUERTA) {
        _p.palCpu += (difCpu > 0) ? PONG_CPU_VEL : -PONG_CPU_VEL;
    }
    if (_p.palCpu < PONG_TOP_Y) _p.palCpu = PONG_TOP_Y;
    if (_p.palCpu > PONG_BOT_Y - PONG_PALETA_H) _p.palCpu = PONG_BOT_Y - PONG_PALETA_H;

    // ── Pausa de saque: las paletas se mueven, la pelota no ──
    if (_p.saqueHasta != 0) {
        if ((int32_t)(now - _p.saqueHasta) < 0) return;
        _p.saqueHasta = 0;
    }

    // ── Pelota ───────────────────────────────────────────────
    _p.bolaX += _p.velX;
    _p.bolaY += _p.velY;

    // Techo y piso
    if (_p.bolaY < PONG_TOP_Y) {
        _p.bolaY = PONG_TOP_Y;
        _p.velY  = -_p.velY;
        sound.play(Melody::BIP);
    } else if (_p.bolaY + PONG_BOLA_LADO > PONG_BOT_Y) {
        _p.bolaY = PONG_BOT_Y - PONG_BOLA_LADO;
        _p.velY  = -_p.velY;
        sound.play(Melody::BIP);
    }

    // Paleta del jugador (izquierda)
    if (_p.velX < 0 &&
        _p.bolaX <= PONG_PALETA_X_JUG + PONG_PALETA_W &&
        _p.bolaX + PONG_BOLA_LADO >= PONG_PALETA_X_JUG &&
        _p.bolaY + PONG_BOLA_LADO >= _p.palJug &&
        _p.bolaY <= _p.palJug + PONG_PALETA_H) {

        _p.bolaX = PONG_PALETA_X_JUG + PONG_PALETA_W;
        _p.velX  = -_p.velX;
        // Efecto según dónde pegó respecto del centro de la paleta: el
        // borde manda la pelota en diagonal, el centro la devuelve plana.
        float rel = ((_p.bolaY + PONG_BOLA_LADO / 2.0f) -
                     (_p.palJug + PONG_PALETA_H / 2.0f)) / (PONG_PALETA_H / 2.0f);
        _p.velY += rel * PONG_BOLA_EFECTO;
        // El tope se compara sobre la MAGNITUD: velX tiene signo distinto en
        // cada paleta, y comparar el valor con signo deja pasar el límite.
        if (fabsf(_p.velX) < PONG_BOLA_VEL_MAX) _p.velX += PONG_BOLA_ACEL;
        sound.play(Melody::BIP);
    }

    // Paleta de la CPU (derecha)
    if (_p.velX > 0 &&
        _p.bolaX + PONG_BOLA_LADO >= PONG_PALETA_X_CPU &&
        _p.bolaX <= PONG_PALETA_X_CPU + PONG_PALETA_W &&
        _p.bolaY + PONG_BOLA_LADO >= _p.palCpu &&
        _p.bolaY <= _p.palCpu + PONG_PALETA_H) {

        _p.bolaX = PONG_PALETA_X_CPU - PONG_BOLA_LADO;
        _p.velX  = -_p.velX;
        float rel = ((_p.bolaY + PONG_BOLA_LADO / 2.0f) -
                     (_p.palCpu + PONG_PALETA_H / 2.0f)) / (PONG_PALETA_H / 2.0f);
        _p.velY += rel * PONG_BOLA_EFECTO;
        if (fabsf(_p.velX) < PONG_BOLA_VEL_MAX) _p.velX -= PONG_BOLA_ACEL;
        sound.play(Melody::BIP);
    }

    // Techo de velocidad vertical: sin esto el efecto se acumula rebote a
    // rebote y la pelota termina viajando casi en vertical.
    if (_p.velY >  PONG_BOLA_VEL_MAX) _p.velY =  PONG_BOLA_VEL_MAX;
    if (_p.velY < -PONG_BOLA_VEL_MAX) _p.velY = -PONG_BOLA_VEL_MAX;

    // ── Puntos ───────────────────────────────────────────────
    bool hubopunto = false;
    if (_p.bolaX + PONG_BOLA_LADO < 0) {
        _p.ptsCpu++;
        hubopunto = true;
        sound.play(Melody::TRISTE);
        _sacar(now, false);
    } else if (_p.bolaX > 128) {
        _p.ptsJug++;
        hubopunto = true;
        sound.play(Melody::FELIZ);
        _sacar(now, true);
    }

    if (hubopunto) {
        Serial.printf("[arcade] Pong %u - %u\n", _p.ptsJug, _p.ptsCpu);
        if (_p.ptsJug >= PONG_PUNTOS_GANAR || _p.ptsCpu >= PONG_PUNTOS_GANAR) {
            _p.ganoJug = (_p.ptsJug > _p.ptsCpu);
            _estado    = ArcadeState::FIN;
            sound.play(_p.ganoJug ? Melody::AMOR : Melody::ENOJADO);
            Serial.printf("[arcade] fin de partida — %s\n",
                          _p.ganoJug ? "gano el jugador" : "gano la CPU");
        }
    }
}

// ============================================================
//  update()
// ============================================================

void Arcade::update(uint32_t now) {
    if (_estado == ArcadeState::JUGANDO) {
        _updatePong(now);
        _tocarActividad(now);   // jugando nunca se considera inactividad
        return;
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
        case ArcadeState::JUGANDO: _renderPong(u8);  break;
        case ArcadeState::PAUSA:   _renderPausa(u8); break;
        case ArcadeState::FIN:     _renderFin(u8);   break;
    }
}

void Arcade::_renderMenu(U8G2& u8) {
    u8.setFont(u8g2_font_6x12_tf);
    const char* titulo = "ARCADE";
    u8.drawStr((128 - u8.getStrWidth(titulo)) / 2, 12, titulo);
    u8.drawHLine(0, 16, 128);

    u8.setFont(u8g2_font_7x13B_tf);
    for (uint8_t i = 0; i < ARCADE_ITEMS_N; i++) {
        int y = 34 + i * 16;
        int x = 34;
        if (i == _sel) {
            // Resaltado en negativo: el ítem elegido se lee de un vistazo
            // incluso con la pantalla lejos.
            u8.drawBox(x - 6, y - 11, 128 - 2 * (x - 6), 14);
            u8.setDrawColor(0);
            u8.drawStr(x, y, ITEMS[i]);
            u8.setDrawColor(1);
        } else {
            u8.drawStr(x, y, ITEMS[i]);
        }
    }
}

void Arcade::_renderPong(U8G2& u8) {
    // Marcador
    char buf[8];
    u8.setFont(u8g2_font_7x13B_tf);
    snprintf(buf, sizeof(buf), "%u", _p.ptsJug);
    u8.drawStr(52 - u8.getStrWidth(buf), 10, buf);
    snprintf(buf, sizeof(buf), "%u", _p.ptsCpu);
    u8.drawStr(76, 10, buf);
    u8.setFont(u8g2_font_5x7_tf);
    u8.drawStr(60, 10, "-");

    // Red central punteada
    for (int y = PONG_TOP_Y; y < PONG_BOT_Y; y += 5) {
        u8.drawVLine(64, y, 3);
    }

    // Paletas
    u8.drawBox(PONG_PALETA_X_JUG, (int)_p.palJug, PONG_PALETA_W, PONG_PALETA_H);
    u8.drawBox(PONG_PALETA_X_CPU, (int)_p.palCpu, PONG_PALETA_W, PONG_PALETA_H);

    // Pelota: durante la pausa de saque titila, para que se vea que va a salir
    bool mostrarBola = true;
    if (_p.saqueHasta != 0) mostrarBola = ((millis() / 150) % 2) == 0;
    if (mostrarBola) {
        u8.drawBox((int)_p.bolaX, (int)_p.bolaY, PONG_BOLA_LADO, PONG_BOLA_LADO);
    }
}

void Arcade::_renderPausa(U8G2& u8) {
    _renderPong(u8);
    // Cartel sobre la cancha, con fondo sólido para que se lea sobre el juego
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
    const char* t = _p.ganoJug ? "GANASTE!" : "PERDISTE";
    u8.drawStr((128 - u8.getStrWidth(t)) / 2, 22, t);

    char buf[16];
    snprintf(buf, sizeof(buf), "%u - %u", _p.ptsJug, _p.ptsCpu);
    u8.setFont(u8g2_font_6x12_tf);
    u8.drawStr((128 - u8.getStrWidth(buf)) / 2, 40, buf);

    u8.setFont(u8g2_font_5x7_tf);
    const char* s = "C revancha   A menu";
    u8.drawStr((128 - u8.getStrWidth(s)) / 2, 58, s);
}
