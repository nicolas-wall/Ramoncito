// ============================================================
//  snake.cpp — Snake sobre grilla
//
//  El cuerpo vive en un buffer circular de celdas. Avanzar es escribir la
//  cabeza nueva y descartar la cola; crecer es simplemente no descartarla.
//  Nada se copia ni se desplaza, así que el costo por frame no depende
//  del largo de la víbora.
// ============================================================
#include "snake.h"
#include "config.h"
#include "input.h"
#include "sound.h"

Snake juegoSnake;

void Snake::begin(uint32_t now) {
    _largo  = 3;
    _cabeza = 0;
    // Arranca en el medio, mirando a la derecha, con el cuerpo detrás.
    uint8_t x0 = SNAKE_COLS / 2, y0 = SNAKE_FILAS / 2;
    for (uint16_t i = 0; i < _largo; i++) {
        _cx[i] = (uint8_t)(x0 - i);
        _cy[i] = y0;
    }
    _cabeza = 0;   // la cabeza es el índice 0 y el cuerpo va hacia atrás
    _dx = 1; _dy = 0;
    _pdx = _dx; _pdy = _dy;
    _puntos = 0;
    _fin = false;
    _proximoPaso = now + SNAKE_PASO_MS;
    _nuevaComida();
}

bool Snake::_ocupada(uint8_t x, uint8_t y) const {
    for (uint16_t i = 0; i < _largo; i++) {
        uint16_t idx = (_cabeza + i) % MAX_LARGO;
        if (_cx[idx] == x && _cy[idx] == y) return true;
    }
    return false;
}

void Snake::_nuevaComida() {
    // Reintenta hasta caer en una celda libre. Con la grilla mayormente
    // vacía esto termina en uno o dos intentos; el tope evita colgarse si
    // alguna vez la víbora ocupa casi todo.
    for (uint8_t intento = 0; intento < 200; intento++) {
        uint8_t x = (uint8_t)(esp_random() % SNAKE_COLS);
        uint8_t y = (uint8_t)(esp_random() % SNAKE_FILAS);
        if (!_ocupada(x, y)) { _comidaX = x; _comidaY = y; return; }
    }
    _comidaX = 0; _comidaY = 0;
}

void Snake::_leerControles() {
    // Palanca: dirección absoluta. No se admite el giro de 180°, que sería
    // morder el propio cuello.
    int8_t nx = _dx, ny = _dy;
    if (input.left()  && _pdx == 0) { nx = -1; ny = 0; }
    else if (input.right() && _pdx == 0) { nx = 1; ny = 0; }
    else if (input.up()   && _pdy == 0) { nx = 0; ny = -1; }
    else if (input.down() && _pdy == 0) { nx = 0; ny = 1; }
    _dx = nx; _dy = ny;
}

// B y C giran en RELATIVO a la dirección actual: B a la izquierda, C a la
// derecha. Es lo que hace a Snake jugable entero con dos botones, sin
// palanca — con direcciones absolutas harían falta cuatro.
void Snake::evento(InputEvent ev, uint32_t now) {
    (void)now;
    if (_fin) return;

    int8_t nx, ny;
    if (ev == InputEvent::BTN_B_PRESS) {          // antihorario
        nx = _dy;  ny = (int8_t)-_dx;
    } else if (ev == InputEvent::BTN_C_PRESS || ev == InputEvent::JOY_SW_PRESS) {
        nx = (int8_t)-_dy;  ny = _dx;             // horario
    } else {
        return;
    }

    // Dos giros seguidos en el mismo sentido dentro de un mismo paso serían
    // media vuelta, o sea morderse el cuello. Se compara contra la dirección
    // ya aplicada, no contra la pendiente.
    if (nx == -_pdx && ny == -_pdy) return;
    _dx = nx; _dy = ny;
}

void Snake::update(uint32_t now) {
    if (_fin) return;

    _leerControles();

    if ((int32_t)(now - _proximoPaso) < 0) return;
    // La cadencia baja con el largo: la víbora acelera a medida que crece,
    // que es de dónde sale toda la dificultad del juego.
    uint32_t paso = SNAKE_PASO_MS - (uint32_t)_puntos * SNAKE_ACEL_MS;
    if (paso < SNAKE_PASO_MIN_MS) paso = SNAKE_PASO_MIN_MS;
    _proximoPaso = now + paso;

    _pdx = _dx; _pdy = _dy;

    int16_t nx = (int16_t)_cx[_cabeza] + _dx;
    int16_t ny = (int16_t)_cy[_cabeza] + _dy;

    // Las paredes matan. Se ve el marco en pantalla, así que el límite es
    // explícito y no una sorpresa.
    if (nx < 0 || nx >= SNAKE_COLS || ny < 0 || ny >= SNAKE_FILAS) {
        _fin = true; sound.play(Melody::TRISTE); return;
    }

    // Chocarse a sí misma. La cola no cuenta: en el mismo paso se libera,
    // así que seguirla de cerca es legal.
    for (uint16_t i = 0; i + 1 < _largo; i++) {
        uint16_t idx = (_cabeza + i) % MAX_LARGO;
        if (_cx[idx] == nx && _cy[idx] == ny) {
            _fin = true; sound.play(Melody::TRISTE); return;
        }
    }

    // Avanzar: la cabeza nueva va una posición ANTES en el buffer circular.
    _cabeza = (_cabeza + MAX_LARGO - 1) % MAX_LARGO;
    _cx[_cabeza] = (uint8_t)nx;
    _cy[_cabeza] = (uint8_t)ny;

    if (nx == _comidaX && ny == _comidaY) {
        if (_largo < MAX_LARGO) _largo++;   // crecer = no descartar la cola
        _puntos++;
        sound.play(Melody::FELIZ);
        _nuevaComida();
    }
}

void Snake::render(U8G2& u8) {
    char buf[16];
    u8.setFont(u8g2_font_5x7_tf);
    snprintf(buf, sizeof(buf), "%u", _puntos);
    u8.drawStr(2, 8, buf);

    // Marco de la cancha
    u8.drawFrame(SNAKE_X0 - 1, SNAKE_Y0 - 1,
                 SNAKE_COLS * SNAKE_CELDA + 2, SNAKE_FILAS * SNAKE_CELDA + 2);

    // Comida: un cuadradito hueco, para distinguirla del cuerpo relleno
    u8.drawFrame(SNAKE_X0 + _comidaX * SNAKE_CELDA,
                 SNAKE_Y0 + _comidaY * SNAKE_CELDA, SNAKE_CELDA, SNAKE_CELDA);

    for (uint16_t i = 0; i < _largo; i++) {
        uint16_t idx = (_cabeza + i) % MAX_LARGO;
        u8.drawBox(SNAKE_X0 + _cx[idx] * SNAKE_CELDA,
                   SNAKE_Y0 + _cy[idx] * SNAKE_CELDA,
                   SNAKE_CELDA - 1, SNAKE_CELDA - 1);
    }
}
