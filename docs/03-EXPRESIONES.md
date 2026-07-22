# Ramoncito — Documento 3: Motor de Cara y Expresiones

## Contexto

Hardware: Seeed XIAO ESP32-S3 · OLED SSD1309 128×64 · I2C · librería U8g2
La cara se dibuja proceduralmente con primitivas U8g2 (sin bitmaps). Esto permite interpolar parámetros entre expresiones para obtener transiciones suaves sin almacenar sprites intermedios.

---

## 1. Anatomía del ojo paramétrico

Cada ojo es un rectángulo redondeado (`drawRBox`) con dos párpados opcionales encima y debajo. Todos los valores son floats que el motor interpola frame a frame.

### 1.1 Parámetros por ojo

| Parámetro       | Tipo  | Descripción                                                                  |
|-----------------|-------|------------------------------------------------------------------------------|
| `cx`, `cy`      | float | Centro del ojo en la pantalla                                                |
| `w`, `h`        | float | Ancho y alto del rectángulo base                                             |
| `r`             | float | Radio de esquinas (0 = rectangular, max = `min(w,h)/2` = ojo circular)      |
| `pTop`          | float | Altura del párpado superior (0 = abierto, `h` = cerrado)                    |
| `pBot`          | float | Altura del párpado inferior (0 = abierto, `h` = cerrado)                    |
| `slopeTop`      | float | Inclinación del párpado superior: Δy en px entre borde izq y der (+= sube izq) |
| `slopeBot`      | float | Inclinación del párpado inferior (mismo convenio)                            |
| `offX`, `offY`  | float | Offset de posición (para mirada errante y micro-movimientos)                 |

### 1.2 Dibujo procedural por ojo

```
1. Dibujar rectángulo redondeado lleno (blanco) de tamaño w×h en (cx+offX, cy+offY)
2. Párpado superior: rect negro de alto pTop con borde inferior inclinado (slopeTop)
   — se dibuja encima del ojo para "tapar" la parte superior
3. Párpado inferior: ídem desde abajo con slopeBot
```

Los párpados se dibujan como polígonos negros (triángulo + rect) sobre el ojo blanco, aprovechando que U8g2 permite dibujar en color `0` (negro) sobre fondo negro o `1` sobre fondo blanco según el modo de dibujo.

### 1.3 Motor de interpolación

```cpp
struct EyeParams {
    float cx, cy, w, h, r;
    float pTop, pBot;
    float slopeTop, slopeBot;
    float offX, offY;
};

// Estado actual (lo que se renderiza)
EyeParams leftCurrent, rightCurrent;

// Estado objetivo (la expresión destino)
EyeParams leftTarget,  rightTarget;

// En update(), cada frame:
void lerpEye(EyeParams& cur, const EyeParams& tgt, float t) {
    cur.cx       = lerp(cur.cx,       tgt.cx,       t);
    cur.cy       = lerp(cur.cy,       tgt.cy,       t);
    cur.w        = lerp(cur.w,        tgt.w,        t);
    cur.h        = lerp(cur.h,        tgt.h,        t);
    cur.r        = lerp(cur.r,        tgt.r,        t);
    cur.pTop     = lerp(cur.pTop,     tgt.pTop,     t);
    cur.pBot     = lerp(cur.pBot,     tgt.pBot,     t);
    cur.slopeTop = lerp(cur.slopeTop, tgt.slopeTop, t);
    cur.slopeBot = lerp(cur.slopeBot, tgt.slopeBot, t);
    // offX/offY se manejan por separado (animaciones idle)
}
```

`t` es el factor de easing por frame. Para una transición de 8 frames suave:  
`t = 0.25` (ease-out exponencial simple: `cur += (tgt - cur) * t`).

---

## 2. Catálogo de expresiones

Coordenadas de referencia con pantalla 128×64:
- Ojo izquierdo centrado en x≈38, ojo derecho en x≈90, ambos en y≈35
- Tamaño base del ojo: w=28, h=22, r=6

La tabla usa notación compacta. `pTop`/`pBot` en px. `slope` positivo = borde izquierdo más alto que derecho.

| Nombre         | pTop L/R | pBot L/R | slopeTop L/R | slopeBot L/R | w/h  | r    | Disparador                        |
|----------------|----------|----------|--------------|--------------|------|------|-----------------------------------|
| **neutral**    | 0 / 0    | 0 / 0    | 0 / 0        | 0 / 0        | 28/22| 6    | Estado base, humor medio          |
| **feliz**      | 8 / 8    | 0 / 0    | 0 / 0        | 0 / 0        | 28/18| 9    | Caricia, comida, felicidad > 70   |
| **triste**     | 3 / 3    | 0 / 0    | −4 / +4      | 0 / 0        | 26/20| 5    | Felicidad < 25, hambre alta       |
| **enojado**    | 7 / 7    | 0 / 0    | +5 / −5      | 0 / 0        | 28/20| 4    | Necesidad ignorada > 10 min       |
| **sorprendido**| 0 / 0    | 0 / 0    | 0 / 0        | 0 / 0        | 30/28| 10   | Evento inesperado, entrada a Pong |
| **aburrido**   | 9 / 9    | 2 / 2    | 0 / 0        | 0 / 0        | 28/18| 6    | Aburrimiento > 70, sin interacción|
| **dormido**    | 11 / 11  | 11 / 11  | 0 / 0        | 0 / 0        | 28/4 | 2    | Ciclo nocturno / energía < 10     |
| **sospechoso** | 5 / 10   | 0 / 0    | +3 / −3      | 0 / 0        | 28/22| 5    | Aleatorio idle raro (v1.5)        |
| **amor/mimo**  | 0 / 0    | 0 / 0    | 0 / 0        | 0 / 0        | 26/26| 13   | Caricia prolongada (> 2 s)        |

Notas:
- **feliz**: párpado superior alto tapa la mitad del ojo → aspecto de "n" o medialuna. El radio grande redondea hacia arriba.
- **triste**: `slopeTop` con signo opuesto en cada ojo → las esquinas internas caen → ceño triste hacia abajo en el centro.
- **enojado**: `slopeTop` con signo opuesto al triste → esquinas internas suben → "V" de ceño fruncido.
- **dormido**: ojo casi colapsado a una línea horizontal de 4 px de alto.
- **amor/mimo**: ojo casi circular (r=13 con w=h=26). En v1.5 se puede añadir un corazón flotante superpuesto dibujado con primitivas.
- **sospechoso**: ojo izquierdo entrecerrado (pTop alto), ojo derecho normal. Las pendientes dan el gesto de duda.

---

## 3. Animaciones de vida (idle)

### 3.1 Parpadeo

```
Intervalo aleatorio: 2000–6000 ms
Secuencia (a 30 fps, cada frame ≈ 33 ms):
  Frame 0–1:  pTop → h*0.9  (ojo casi cerrado)
  Frame 2:    pTop → h      (cerrado)
  Frame 3–4:  pTop → 0      (reabre)
Total: ~5 frames = ~165 ms
```

El parpadeo ignora el párpado objetivo de la expresión actual: lleva `pTop` al máximo y lo devuelve. Al terminar retoma el valor objetivo de la expresión activa.

### 3.2 Mirada errante

```
Cada 3000–8000 ms: elegir (offX, offY) aleatorio dentro de ±5 px
Interpolar suavemente (t=0.12) hacia el nuevo offset
Ambos ojos se mueven juntos (misma dirección)
```

### 3.3 Micro-movimientos

Ruido de baja frecuencia (≈ 0.3 Hz) en `cy` de ambos ojos: ±1 px, da sensación de respiración. Implementar con una senoidal lenta o un random walk muy amortiguado.

### 3.4 Animaciones raras (v1.5)

| Nombre        | Frecuencia aprox. | Descripción                                                               |
|---------------|-------------------|---------------------------------------------------------------------------|
| Bostezo       | cada 5-10 min     | Ojo se abre mucho (sorprendido), luego se cierra lento, vuelve a abrir    |
| Mirada fija   | cada 3-7 min      | offX/Y = 0, párpados ligeramente bajados, sin parpadear por 4 s           |
| Sacudida      | cada 8-15 min     | Vibración rápida en offX ±3 px, 8 frames, como si sacudiera la cabeza     |

---

## 4. Máquina de estados de la cara

```
                    ┌──────────────────────┐
                    │        IDLE          │
                    │  expresión según mood│◄──────────────────┐
                    │  + animaciones idle  │                   │
                    └──────────┬───────────┘                   │
                               │ evento                        │
                               ▼                               │
                    ┌──────────────────────┐          animación
                    │     REACCIÓN         │          terminada │
                    │  expresión fija N    │──────────────────►┘
                    │  frames, sin idle    │
                    └──────────────────────┘
                               │
                               │ [ciclo nocturno OR energía < 10]
                               ▼
                    ┌──────────────────────┐
                    │     DURMIENDO        │
                    │  expresión dormido   │
                    │  animación Zzz       │
                    └──────────┬───────────┘
                               │ botón presionado OR energía > 20
                               ▼
                    ┌──────────────────────┐
                    │     DESPERTAR        │
                    │  animación 1.5 s     │──► IDLE
                    └──────────────────────┘
```

### Transiciones de estado

| Desde        | Hacia       | Condición                                              | Duración reacción |
|--------------|-------------|--------------------------------------------------------|-------------------|
| IDLE         | REACCIÓN    | Evento externo (botón, touch, Pong, etc.)              | 60–120 frames     |
| REACCIÓN     | IDLE        | Contador de frames agotado                             | —                 |
| IDLE         | DURMIENDO   | Hora nocturna (22:00–7:00) OR energía < 10             | transición 45 f   |
| DURMIENDO    | DESPERTAR   | Botón presionado OR energía recargada > 20             | 45 frames         |
| DESPERTAR    | IDLE        | Animación terminada                                    | —                 |

---

## 5. Vínculo con el sistema de humor

Las variables del módulo `mood` son enteros 0–100. La expresión idle dominante se selecciona según la siguiente tabla de prioridad (de mayor a menor):

| Condición                              | Expresión idle dominante |
|----------------------------------------|--------------------------|
| energía < 10                           | dormido                  |
| felicidad < 20                         | triste                   |
| felicidad < 20 Y aburrimiento > 60     | enojado                  |
| aburrimiento > 75                      | aburrido                 |
| felicidad > 70 Y aburrimiento < 40     | feliz                    |
| (ninguna anterior)                     | neutral                  |

La expresión se actualiza solo al cambiar de estado REACCIÓN → IDLE (no en medio de una reacción en curso).

---

## 6. Sonidos asociados

Descripciones de comportamiento. La implementación PWM del buzzer va en el módulo `sound`.

| Evento                       | Sonido                                                               |
|------------------------------|----------------------------------------------------------------------|
| Despertar                    | Melodía corta ascendente de 3 notas (Do-Mi-Sol), ~400 ms            |
| Expresión feliz disparada    | Dos bips agudos rápidos (~800 Hz, 50 ms cada uno)                   |
| Expresión amor/mimo          | Tono suave pulsado (~600 Hz, modulado en amplitud) durante la caricia|
| Expresión enojado            | Bip grave corto (~200 Hz, 150 ms) con pequeño glissando descendente  |
| Expresión triste             | Bip descendente lento (~500→300 Hz, 300 ms)                         |
| Bostezo (v1.5)               | Tono modulado en frecuencia, sube y baja en ~800 ms                  |
| Ronquido en DURMIENDO        | Bip grave periódico (~150 Hz, 80 ms) cada ~3 s, aleatorizado ±0.5 s  |
| Transición a DURMIENDO       | Melodía descendente de 4 notas, ralentizándose                       |
