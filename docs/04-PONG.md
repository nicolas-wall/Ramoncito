# espToy — Documento 4: Minijuego Oculto — Pong

## Contexto

Hardware: XIAO ESP32-S3 · OLED SSD1309 128×64 · U8g2 · botones A (GPIO) y B (GPIO) · buzzer pasivo · loop a 30 fps sin delays bloqueantes.

---

## 1. Entrada secreta

### Detección del combo

```
Estado: IDLE (cara activa, no en animación de reacción)
Condición: botón A AND botón B mantenidos simultáneamente ≥ 3000 ms

Implementación:
  - comboStartMs = 0
  - Si A y B están presionados y comboStartMs == 0: comboStartMs = millis()
  - Si A o B se suelta: comboStartMs = 0
  - Si millis() - comboStartMs >= 3000: disparar entrada a Pong
```

Durante la cuenta regresiva (0–3 s) no hacer nada visible. El combo solo se activa si la cara está en estado IDLE; en DURMIENDO y REACCIÓN no se procesa.

### Animación de transición — entrada

1. **Frame 0–15**: expresión SORPRENDIDO en la cara (ojos grandes redondos), bip de inicio.
2. **Frame 16–30**: barrido horizontal — una línea negra recorre la pantalla de izquierda a derecha cubriendo la cara, dejando el campo de Pong.
3. **Frame 31**: pantalla completamente en Pong, jingle de inicio de partida.

Total transición entrada: ~1 s.

### Salida del minijuego

Opciones para salir:
- Mismo combo A+B 3 s (desde Pong activo, no desde pausa).
- Fin de partida (alguien llega a 5 puntos) → salida automática tras pantalla de resultado.

Animación de transición de salida: barrido inverso (derecha a izquierda), cara reaparece con expresión de reacción según resultado (ver sección 5).

---

## 2. Reglas del juego

### Campo

```
┌────────────────────────────────────────────────────────────────────────────────┐
│                              3  :  2                                           │ ← marcador (y=0..8)
│                                  .                                             │
│ ██  ←paleta                       ·                          ██ ←paleta CPU   │
│ ██   jugador                       ○ ←pelota                 ██               │
│ ██                                                            ██               │
│                                                                                │
└────────────────────────────────────────────────────────────────────────────────┘
 x=0                                                                         x=127
```

- **Área de juego**: 128 × 55 px (filas 9–63, dejando 9 px arriba para el marcador).
- **Paleta jugador** (izquierda): x = 4, ancho = 3 px, alto = 12 px.
- **Paleta CPU** (derecha): x = 121, ancho = 3 px, alto = 12 px.
- **Pelota**: círculo de radio 2 px (5×5 px con `drawDisc`).
- **Marcador**: centrado en y = 4, formato `"N  :  M"` con `setFont(u8g2_font_tiny5_tf)` o equivalente de 5 px de alto.

### Puntuación

- Pelota sale por el borde izquierdo → punto para la CPU.
- Pelota sale por el borde derecho → punto para el jugador.
- Al marcar punto: pelota reinicia al centro, dirección hacia quien recibió el punto, velocidad inicial.
- Primero en llegar a **5 puntos** gana la partida.

### Controles

| Botón | Acción            |
|-------|-------------------|
| A     | Paleta jugador sube (↑) |
| B     | Paleta jugador baja (↓) |
| A+B 3s| Salir al menú          |

---

## 3. Física de la pelota

### Variables de estado

```cpp
float ballX, ballY;   // posición del centro
float ballVX, ballVY; // velocidad en px/frame
```

### Velocidad inicial y aceleración

```
Velocidad inicial: |vX| = 2.5,  |vY| = 1.2  (px/frame a 30 fps)
Tras cada rebote en paleta:
    speed = sqrt(vX² + vY²) * 1.06   // +6%
    speed = min(speed, 6.0)           // tope absoluto
    vX, vY se reescalan para mantener dirección
```

### Rebote en bordes superior/inferior

```
Si ballY - r < 9  (techo del área de juego):
    ballY = 9 + r;  vY = abs(vY);   bip rebote
Si ballY + r > 63 (suelo):
    ballY = 63 - r; vY = -abs(vY);  bip rebote
```

### Rebote en paleta — ángulo según impacto

El ángulo de salida depende de la distancia del punto de impacto al centro de la paleta, normalizado a [-1, 1]:

```cpp
float hitPos = (ballY - paddleY) / (PADDLE_H / 2.0f);
// hitPos en [-1, 1]: -1 = borde top, 0 = centro, +1 = borde bottom

float bounceAngle = hitPos * MAX_BOUNCE_ANGLE;  // MAX_BOUNCE_ANGLE = 60°

float speed = sqrt(vX*vX + vY*vY) * 1.06f;
speed = min(speed, MAX_SPEED);

// Paleta izquierda (jugador): pelota sale hacia la derecha
vX =  speed * cos(bounceAngle);
vY =  speed * sin(bounceAngle);

// Paleta derecha (CPU): pelota sale hacia la izquierda
vX = -speed * cos(bounceAngle);
vY =  speed * sin(bounceAngle);
```

Pegar con el borde (|hitPos| → 1) da hasta ±60° de ángulo; el centro da tiro horizontal puro.

### Detección de colisión con paletas

Colisión caja–caja entre el AABB de la pelota y el rect de la paleta. Comprobar solo cuando la pelota se aproxima (vX < 0 para paleta izquierda, vX > 0 para paleta derecha) para evitar dobles rebotes.

---

## 4. IA de la CPU

### Movimiento base

```cpp
float cpuCenter = cpuPaddleY + PADDLE_H / 2.0f;
float targetY   = ballY;                          // sigue a la pelota

float diff = targetY - cpuCenter;
float maxSpeed = CPU_BASE_SPEED + scoreDifficulty;

if (abs(diff) > 1.0f) {
    cpuPaddleY += clamp(diff, -maxSpeed, maxSpeed);
}
```

### Error aleatorio

Cada 40–80 frames (intervalo aleatorizado): añadir un offset al `targetY` de la CPU:

```cpp
cpuTargetOffset = random(-CPU_ERROR_RANGE, CPU_ERROR_RANGE);
// CPU_ERROR_RANGE = 8 px en dificultad 0, decrece con dificultad
```

El offset decae linealmente a 0 durante el próximo intervalo.

### Dificultad progresiva

| Puntos del jugador | `CPU_BASE_SPEED` (px/frame) | `CPU_ERROR_RANGE` (px) |
|--------------------|------------------------------|------------------------|
| 0                  | 1.8                          | 10                     |
| 1                  | 2.1                          | 8                      |
| 2                  | 2.4                          | 6                      |
| 3                  | 2.7                          | 4                      |
| 4                  | 3.0                          | 2                      |

La CPU nunca supera la velocidad máxima de la pelota. El error garantiza que el jugador pueda ganar con buena técnica de ángulo.

---

## 5. Integración con el personaje

### Al terminar la partida

La pantalla muestra el resultado durante 2 s, luego transición de salida a cara.

| Resultado     | Expresión de cara al volver | Efecto en mood                              | Sonido       |
|---------------|-----------------------------|---------------------------------------------|--------------|
| Jugador gana  | ENOJADO / puchero (tristeza)| felicidad −5, aburrimiento −40              | Jingle derrota CPU (burlesco) |
| CPU gana      | AMOR/burlón (feliz exagerado)| felicidad +10, aburrimiento −40             | Jingle victoria CPU (fanfarria corta) |

Independientemente del resultado:
- `aburrimiento -= 40` (mínimo 0). Jugar siempre reduce el aburrimiento significativamente.
- La expresión de reacción dura 90 frames (~3 s) antes de volver al idle dominante del mood.

**Narrativa del personaje**: el personaje es la mascota, no el jugador de Pong. Si el jugador humano gana al Pong, la mascota "pierde" → se enoja/hace puchero. Si la CPU gana, la mascota "gana" → festeja burlona.

---

## 6. Sonido

| Evento                        | Descripción                                                          |
|-------------------------------|----------------------------------------------------------------------|
| Rebote en paleta              | Bip corto agudo (~900 Hz, 25 ms)                                    |
| Rebote en pared (top/bot)     | Bip medio (~600 Hz, 20 ms)                                           |
| Punto marcado                 | Bip grave descendente (~400→200 Hz, 150 ms)                          |
| Inicio de partida (jingle)    | 3 notas ascendentes: Sol-La-Si, 80 ms cada una                       |
| Victoria CPU (jingle fin)     | Melodía de 5 notas rápidas ascendentes tipo fanfarria                |
| Derrota CPU / victoria humana | 4 notas descendentes con tempo lento, tono "decepcionado"            |

Todos los sonidos no bloquean: se disparan con `sound.play(note, duration)` que usa el timer PWM del ESP32-S3 y el loop continúa.

---

## 7. Detalles de implementación

### Estructura del módulo

```cpp
class PongGame {
public:
    void enter();          // inicializa estado, arranca jingle de inicio
    void exit();           // limpia estado
    void update();         // lógica a 30 fps, sin delays
    void render(U8G2& u8); // dibuja con primitivas, sin bitmaps
    bool isActive();
    GameResult getResult();// PLAYER_WINS, CPU_WINS, IN_PROGRESS
};
```

### Loop principal (extracto)

```cpp
// En el loop() del firmware:
uint32_t now = millis();
if (now - lastFrameMs >= 33) {          // ~30 fps
    lastFrameMs = now;

    if (pong.isActive()) {
        pong.update();
        u8g2.clearBuffer();
        pong.render(u8g2);
        u8g2.sendBuffer();
    } else {
        face.update();
        u8g2.clearBuffer();
        face.render(u8g2);
        u8g2.sendBuffer();
    }
}
```

### Primitivas U8g2 usadas

| Elemento      | Primitiva U8g2                   |
|---------------|----------------------------------|
| Pelota        | `drawDisc(x, y, 2)`              |
| Paletas       | `drawBox(x, y, w, h)`            |
| Marcador      | `drawStr(x, y, scoreStr)`        |
| Línea central | `drawVLine(64, 9, 55)` (opcional, cada 4 px) |

### Restricciones

- **Sin `delay()`** en todo el módulo. Toda lógica temporal se maneja con `millis()` y contadores de frames.
- **Sin `malloc()` dinámico**: todas las estructuras son miembros de la clase o variables estáticas.
- El módulo Pong no accede directamente al sistema de mood: devuelve `GameResult` al final y el sistema central aplica los efectos.
- La detección del combo A+B 3 s se procesa en el módulo de input, no en Pong ni en Face.
