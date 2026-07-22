# Ramoncito — Documento 2: Arquitectura de Software

> Documento 2 de la serie de planificación de Ramoncito. Define la estructura del firmware: módulos, responsabilidades, flujo de datos y convenciones. Los detalles de cada subsistema viven en los docs 03 (cara), 04 (Pong) y 01 (hardware).

---

## 1. Principios de diseño

1. **Nada bloquea, nunca.** Un solo loop cooperativo a ~30 fps gobernado por `millis()`. Prohibido `delay()` fuera del `setup()`. Si la cara se congela aunque sea medio segundo, el personaje "muere" — la fluidez ES el producto.
2. **Módulos con una responsabilidad**, comunicados por eventos y lecturas de estado, no por llamadas cruzadas. Pong no sabe que existe el mood; el input no sabe qué hace un botón; el orquestador (`main`) conecta todo.
3. **Sin asignación dinámica en runtime.** Todo estático o miembro de clase. En un firmware que corre semanas, `malloc` en el loop es fragmentación garantizada.
4. **Dibujo procedural, no bitmaps.** Las expresiones son parámetros interpolables (doc 03), no sprites. Excepción permitida: iconos puntuales (Zzz, corazón) si conviene.
5. **Todo lo que suena o anima es interrumpible** y vuelve solo al estado base.

---

## 2. Estructura de archivos (PlatformIO)

```
Ramoncito/
├── platformio.ini
├── docs/                     ← esta serie de documentos
├── include/
│   └── config.h              ← pines, umbrales, constantes de timing (UN solo lugar)
└── src/
    ├── main.cpp              ← setup(), loop(), orquestador y máquina de estados global
    ├── face.h / face.cpp     ← motor de ojos: EyeParams, interpolación, expresiones, idle
    ├── mood.h / mood.cpp     ← cerebro Tamagotchi: variables, decaimiento, NVS
    ├── input.h / input.cpp   ← botones (debounce), touch (autocalibración), combos
    ├── sound.h / sound.cpp   ← buzzer LEDC, cola de notas no bloqueante, mute
    ├── pong.h / pong.cpp     ← minijuego completo, autocontenido
    └── net.h / net.cpp       ← WiFi + NTP, hora local, ventana nocturna
```

`config.h` centraliza **todo** número mágico: pines (según doc 01), umbrales de mood, tiempos de animación, rango nocturno, dirección I2C. Cambiar un pin o un tiempo nunca requiere tocar un `.cpp`.

### platformio.ini

```ini
[env:seeed_xiao_esp32s3]
platform = espressif32
board = seeed_xiao_esp32s3
framework = arduino
monitor_speed = 115200
lib_deps =
    olikraus/U8g2
build_flags =
    -D CORE_DEBUG_LEVEL=1
```

Credenciales WiFi: en `include/secrets.h` (plantilla `secrets.h.example` versionada; el real en `.gitignore`).

---

## 3. Máquina de estados global (AppState)

`main.cpp` es dueño del estado global. Los módulos no cambian de estado por su cuenta: emiten eventos/resultados y el orquestador decide.

```
             evento de input                  animación terminada
        ┌──────────────────────►┌───────────┐──────────────────┐
        │                       │ REACTING  │                  │
┌───────┴───┐                   └───────────┘                  ▼
│   IDLE    │◄──────────────────────────────────────────────────
│ (cara +   │
│  humor)   │──── combo A+B 3s ────►┌───────────┐
└───────────┘◄─── fin de partida ───│   PONG    │
      │  ▲        o combo de salida └───────────┘
      │  │
 hora nocturna o          botón (protesta y sigue durmiendo)
 energía < 10             o fin de ventana nocturna
      │  │
      ▼  │
┌───────────┐
│ SLEEPING  │
└───────────┘
```

| Estado | Quién renderiza | Entradas activas | Mood |
|---|---|---|---|
| `IDLE` | `face` (expresión según mood + animaciones de vida) | botones, touch, combo | decae normal |
| `REACTING` | `face` (expresión fija N frames) | se encolan, no interrumpen | recibe los efectos del evento |
| `PONG` | `pong` | botones = paletas, combo = salir | congelado (solo aplica resultado al salir) |
| `SLEEPING` | `face` (dormido + Zzz) | botón → protesta breve | energía se recarga |

Detalle de transiciones y duraciones: doc 03 §4 (cara) y doc 04 §1 (Pong).

---

## 4. Loop principal

```cpp
void loop() {
    uint32_t now = millis();

    // 1. Entradas: siempre, en cada pasada (máxima resolución de debounce)
    input.poll(now);

    // 2. Lógica + render: solo al tick de frame (~33 ms → 30 fps)
    if (now - lastFrameMs < FRAME_MS) return;
    lastFrameMs = now;

    // 3. Despachar eventos de input según AppState (orquestador)
    dispatchEvents();

    // 4. Actualizar el módulo activo
    mood.update(now);                 // decaimiento (interno usa timer de 60 s)
    sound.update(now);                // avanza la cola de notas sin bloquear
    switch (appState) {
        case PONG:  pong.update();  break;
        default:    face.update(now, mood.dominantExpression()); break;
    }

    // 5. Render del módulo activo
    u8g2.clearBuffer();
    (appState == PONG) ? pong.render(u8g2) : face.render(u8g2);
    u8g2.sendBuffer();
}
```

Notas:
- `input.poll()` corre a máxima frecuencia (fuera del gate de frame) para que el debounce y el combo tengan resolución fina; el resto va al ritmo del frame.
- `sendBuffer()` por I2C a 400 kHz (`Wire.setClock(400000)`) tarda ~25 ms para el buffer completo de 1 KB — es el techo real del framerate. Si 30 fps quedan justos, el plan B es modo page buffer o aceptar ~25 fps (imperceptible para esta estética).

---

## 5. Contratos entre módulos

### input → orquestador (eventos)

```cpp
enum class InputEvent : uint8_t {
    NONE, BTN_A, BTN_B, BTN_A_LONG, BTN_B_LONG,
    TOUCH_START, TOUCH_HOLD_2S, TOUCH_END,
    COMBO_AB_3S
};
InputEvent input.nextEvent();   // cola FIFO chica (estática, 8 slots)
```

`input` no sabe qué significa cada evento; solo detecta. El significado (sorpresa, caricia, entrar al Pong) lo asigna `dispatchEvents()` según el estado.

### mood ↔ resto

```cpp
mood.apply(MoodEffect::CARICIA);      // tabla de efectos en config.h
Expression mood.dominantExpression(); // tabla de prioridad del doc 03 §5
mood.update(now);                     // decaimiento + persistencia diferida
```

Persistencia: `Preferences` (NVS). Se guarda **como máximo cada 5 minutos** y solo si algo cambió (dirty flag), para no desgastar la flash; además, guardado **inmediato tras interacciones importantes** (caricia, partida de Pong), que son pocas por día. Al boot: cargar o inicializar en (felicidad 50, energía 80, aburrimiento 0).

**Decaimiento offline**: junto al estado se guarda `lastSavedEpoch` (hora del último guardado). Al boot, si hay hora válida en ambos extremos (la guardada y la actual), se calcula el tiempo que estuvo apagado y se aplica el decaimiento equivalente, con **tope de 48 h** (que vuelva aburrido y ofendido, no clínicamente deprimido). Si falta hora válida en cualquiera de los dos momentos, se omite y el apagado actúa como pausa.

### sound (no bloqueante)

```cpp
sound.play(Melody::FELIZ);   // encola; update() la reproduce nota a nota con LEDC
sound.stop();
sound.setEnabled(bool);      // mute global (buzzer ausente o modo noche)
```

Melodías = arrays estáticos `{freq, ms}` en flash (`PROGMEM` no es necesario en ESP32, pero sí `const`).

### pong (autocontenido)

Interfaz exacta en doc 04 §7: `enter() / exit() / update() / render() / getResult()`. El orquestador aplica el resultado al mood al salir.

### net (solo al boot)

```cpp
net.syncTime();                  // WiFi → NTP → configura RTC interno → desconecta WiFi
bool net.isNight();              // usa hora local del RTC; si nunca sincronizó → false
net.startConfigPortal();         // AP "Ramoncito-setup" + portal cautivo (ver abajo)
net.setTimeFromClient(epoch);    // hora recibida del navegador del teléfono
```

WiFi se desconecta tras sincronizar (ahorro y menos ruido para el touch). Resincronización: una vez por día si está despierto, con reconexión breve. Fallback sin WiFi: nunca es de noche, todo lo demás funciona (doc 05, riesgos).

**Configuración desde el teléfono (portal cautivo)**: si no hay credenciales guardadas o la conexión falla, el toy levanta su propio access point `Ramoncito-setup`. Te conectás desde el teléfono y se abre sola una página servida por el ESP32 donde: (a) elegís tu red WiFi de una lista y ponés la clave — se guarda en NVS, nunca en el código —, y (b) hay un botón "usar la hora de este teléfono": el navegador manda su hora local (JavaScript `Date.now()` + offset de zona) y el RTC queda seteado **sin necesidad de internet ni de router**. Las credenciales en `secrets.h` quedan como default opcional de desarrollo; la fuente primaria es el portal.

---

## 6. Manejo del tiempo

| Mecanismo | Uso |
|---|---|
| `millis()` + timestamps | todo el timing de gameplay/animación |
| Contadores de frames | duración de reacciones y transiciones (doc 03/04) |
| RTC interno (post-NTP) | hora del día para el ciclo nocturno |
| `esp_random()` | intervalos de parpadeo, errores de la IA, variaciones |

Regla: ningún módulo asume que `update()` se llama a intervalos exactos; siempre recibir/leer `now` y calcular deltas. Esto hace todo robusto a frames largos ocasionales (ej. escritura NVS).

**Modo test**: `config.h` define `TIME_SCALE` (1 en producción). Con `TIME_SCALE 60`, un minuto de decaimiento de humor pasa en un segundo y la noche se puede simular — clave para probar la Etapa 5 y 6 sin esperar horas. Complemento: comandos por serial (`h 23` fuerza hora, `m 10 80 90` fuerza mood, `s` toggle sonido).

---

## 7. Orden de arranque (`setup()`)

1. Serial (115200) y mensaje de boot con versión.
2. NVS → cargar mood.
3. I2C (`Wire.begin(); Wire.setClock(400000)`) → `u8g2.begin()` → splash breve (el personaje "despierta": animación de apertura de ojos + melodía de boot).
4. Input: pines botones `INPUT_PULLUP`; touch: autocalibración (50 lecturas, descartar outliers, baseline + umbral relativo — doc 01 §4.3 y doc 05 riesgos).
5. Sound: LEDC canal 0 en GPIO4.
6. Net: intento WiFi+NTP con timeout 10 s **en paralelo visual** — la animación de despertar corre mientras tanto (el intento WiFi se hace con eventos, no bloqueante; o se tolera que el splash tape la espera).
7. Entrar a `IDLE`.

---

## 8. Convenciones de código

- C++ estilo Arduino/ESP32: clases simples, sin excepciones, sin STL pesada (está permitido `<array>`, evitá `std::vector` en runtime).
- Nombres: módulos en minúscula (`face`, `mood`), tipos en PascalCase (`EyeParams`, `InputEvent`), constantes `SCREAMING_SNAKE` en `config.h`.
- Cada `.h` expone una única clase-instancia global (patrón singleton simple: `extern Face face;`).
- Logs por serial con prefijo de módulo: `[mood] boredom=82 -> expresion ABURRIDO`.
- Un commit por etapa del roadmap como mínimo (ver doc 05), mensajes en inglés.
