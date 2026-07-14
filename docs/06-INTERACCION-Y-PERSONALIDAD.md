# 06 — Interacción, Animaciones y Personalidad

Diseño acordado el 2026-07-13. Reemplaza las reglas de interacción del doc 03
donde entren en conflicto. Se implementa en etapas A–E (ver §7).

---

## 1. Reglas de interacción

### 1.1 De día (despierto)

| Entrada | Efecto |
|---|---|
| **Caricia** (touch cabeza D2) | Siempre le gusta, sin límite. Cara AMOR + felicidad +20, aburrimiento −15. Nunca lo molesta. |
| **Cosquillas** (touch pie D8) | Le gustan hasta `TICKLE_SEGUIDAS_MAX` (3 base; 2 si es gruñón) dentro de la ventana de 6 s. Cara FELIZ + felicidad +10. Al pasarse → se enoja. |
| **Botón** (D0) | Solo utilidad: abre el menú de status. Nunca afecta el humor. |

### 1.2 Mal humor (cooldown después del enojo)

Cuando se enoja por cosquillas de más:

- Entra en **malhumor** por `MALHUMOR_MS` (60 s base; ×2 si es gruñón, ÷2 si es alegre).
- Durante el malhumor la cara idle es ENOJADO (override del humor dominante).
- Cosquillas durante el malhumor: renuevan el enojo y extienden el malhumor.
  No suman felicidad.
- Una caricia durante el malhumor **lo perdona antes**: corta el malhumor a la
  mitad del tiempo restante y reacciona con AMOR normal.
- Al terminar el malhumor, el contador de cosquillas vuelve a 0 (las 3 de nuevo).

### 1.3 De noche (durmiendo, 22:00–07:00)

| Entrada | Efecto |
|---|---|
| **1 caricia** (ninguna otra en los últimos 30 s) | Cara FELIZ ~2 s **sin despertarse**, después vuelve a DORMIDO. Solo visual: no cambia el humor. |
| **2.ª caricia dentro de 30 s** | Se despierta enojado: cara ENOJADO + sonido ~2.5 s, y **vuelve a dormir solo**. No cambia el humor, pero cuenta para la personalidad (gruñón +). |
| **Cualquier cosquilla** | Igual que la 2.ª caricia: enojo nocturno directo. |
| **Botón** | Abre el menú normalmente (utilidad, no lo "molesta"). |

### 1.4 Aburrimiento por inactividad

- Cada `30 min` sin interacción → cara "¿qué pasa? me aburro" (SOSPECHOSO con
  su animación de "?"), durante ~4 s o hasta que alguien interactúe.
- Se repite cada 30 min mientras siga sin interacción.
- Standby (pantalla off) a la hora de inactividad, como está.

### 1.5 Caras según humor (sin cambios de mapeo)

`dominantExpression()` sigue eligiendo la cara idle: NEUTRAL, FELIZ (muy
contento), ABURRIDO, TRISTE, ENOJADO, DORMIDO (siesta). Los umbrales se
modulan por personalidad (§3.4).

---

## 2. Animaciones por expresión

Cada expresión deja de ser una pose estática: tiene **intro → loop → outro**.

### 2.1 Motor (en `face.cpp`)

- Fases: `INTRO` (~400 ms, easing con leve overshoot), `LOOP` (continuo),
  `OUTRO` (~250 ms, squash de los ojos antes de cambiar).
- `setExpression(e)` con expresión distinta: ejecuta OUTRO de la actual,
  cambia parámetros y corre INTRO de la nueva, luego queda en LOOP.
- **Sistema de partículas** para los extras (máx. 4 simultáneas): corazones,
  Z's, lágrima, "?", puntos suspensivos. Cada partícula: tipo, posición,
  velocidad, edad, vida, escala.
- El parpadeo, la mirada errante y la respiración actuales se mantienen como
  capa base del LOOP donde aplique.

### 2.2 Comportamiento por expresión

| Expresión | Loop | Extras animados |
|---|---|---|
| NEUTRAL | parpadeo + mirada errante + respiración (actual) | — |
| FELIZ | rebote vertical suave ±2 px, ritmo alegre | — |
| AMOR | ojos pulsan tamaño ±10 % | corazones (hasta 3) naciendo cerca de los ojos, flotan hacia arriba con deriva, crecen y se desvanecen |
| DORMIDO | respiración lenta y amplia | Z's subiendo en diagonal, tamaño creciente, respawn continuo |
| ENOJADO | temblor horizontal ±1 px aleatorio | rayitas de furia arriba a los costados, parpadeando |
| TRISTE | párpados caídos con micro-temblor | lágrima que crece en el ojo izquierdo y cae, cada ~4 s |
| ABURRIDO | párpado superior baja lentamente hasta casi cerrar y reabre (ciclo ~5 s) | "…" apareciendo de a un punto |
| SORPRENDIDO | pupila pulsa | intro con "pop" (overshoot 1.3×) |
| SOSPECHOSO | la mirada barre lento izq↔der | "?" flotando arriba a la derecha, parpadea |
| GUINO | one-shot: cierra ojo derecho 300 ms, mantiene 500 ms, abre | — |

---

## 3. Personalidad

### 3.1 Modelo

4 ejes **independientes** (pueden coexistir), 0–100, inicial 50:

- `alegre` — tendencia a estar contento
- `grunon` — tendencia a enojarse
- `energetico` — tendencia a la actividad
- `perezoso` — tendencia a dormir/vaguear

### 3.2 Aprendizaje

**Muestreo pasivo** — en cada tick de humor (5 min, despierto): según la
expresión dominante del momento:

| Dominante | Ajuste |
|---|---|
| FELIZ | alegre +1, grunon −1 |
| ENOJADO | grunon +2, alegre −1 |
| TRISTE | grunon +1, alegre −1 |
| DORMIDO (siesta diurna) | perezoso +2, energetico −1 |
| ABURRIDO | perezoso +1 |
| NEUTRAL | — |

**Eventos** — al ocurrir:

| Evento | Ajuste |
|---|---|
| Caricia | alegre +1 |
| Cosquillas (bien recibidas) | alegre +1, energetico +1 |
| Enojo por cosquillas | grunon +2 |
| Despertado de noche (enojo nocturno) | grunon +2 |
| Cualquier interacción | energetico +1 |

### 3.3 Plasticidad (formación vs. madurez)

- Se guarda `birthEpoch` en NVS la primera vez que hay hora válida.
- **Primeros 7 días**: los ajustes se aplican ×1.0 (personalidad en formación).
- **Después**: ×0.25 — puede cambiar, pero cuesta mucho más. Volver de gruñón
  a alegre es posible pero requiere mucho trabajo sostenido.
- Sin hora válida todavía: se asume en formación (×1.0).

### 3.4 Efectos sobre el comportamiento (rasgo > 65 = alto)

| Rasgo alto | Efecto |
|---|---|
| **grunon** | Se enoja con 2 cosquillas (no 3). Malhumor dura ×2. Umbral de cara ENOJADO más fácil de alcanzar (felicidad < 25 y aburrimiento > 50). |
| **alegre** | La felicidad decae más lento (−1/tick en vez de −3). Malhumor dura ÷2. Cara FELIZ más fácil (felicidad > 60). |
| **energetico** | La energía decae a la mitad. Recupera más rápido durmiendo (+35/tick). |
| **perezoso** | La energía decae al doble. Entra en siesta antes (energía < 10) y recupera más lento (+15/tick), así la siesta dura más. |

Si dos rasgos opuestos están altos a la vez, los efectos se combinan (se
compensan parcialmente). Persistencia: 4 bytes + birthEpoch en NVS.

### 3.5 Visualización

Página 2 del menú, en **palabras** por rasgo (bajo < 35 / medio / alto > 65):

- alegre: `serio · alegre · muy alegre`
- grunon: `tranqui · algo grunon · muy grunon`
- energetico: `calmo · activo · muy activo`
- perezoso: `inquieto · algo vago · muy vago`

Más la edad en días ("5 dias" / "en formacion" si no hay hora).

---

## 4. Menú de 2 páginas (1 botón)

1. 1.ª presión → página 1: stats actual (hora, WiFi, barras de humor).
2. 2.ª presión → página 2: personalidad + edad.
3. 3.ª presión → cierra.
4. Timeout de 10 s por página (cierra el menú).
5. Reservado: páginas futuras para NFC e info extra (v2).

---

## 5. Sonido

Siempre hay sonido en las reacciones (positivas y negativas), incluido el
enojo nocturno. Melodías existentes: AMOR, FELIZ, ENOJADO, BIP, DORMIR,
DESPERTAR, RONQUIDO, BOOT.

---

## 6. Backlog v2 (recordatorios)

- NFC (PN532): interacción entre juguetes / tags — la página 2+ del menú ya
  deja lugar para esto.
- RTC DS3231, MPU-6050, batería LiPo + interruptor.

---

## 7. Etapas de implementación

| Etapa | Contenido | Archivos |
|---|---|---|
| **A** | Reglas de interacción: malhumor, caricia nocturna visual, enojo nocturno, "qué pasa" cada 30 min | `main.cpp`, `config.h` |
| **B** | Módulo `personality` (aprendizaje, plasticidad, NVS) + modulación de mood/config | `personality.h/.cpp`, `mood.cpp`, `main.cpp` |
| **C** | Menú de 2 páginas con personalidad | `menu.h/.cpp`, `main.cpp` |
| **D** | Motor de animaciones intro/loop/outro + partículas | `face.h/.cpp`, `config.h` |
| **E** | Integración, compilación y prueba en hardware | — |
