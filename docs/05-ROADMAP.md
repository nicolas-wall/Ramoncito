# espToy — Roadmap de construcción
<!-- Documento 5 de la serie de planificación de espToy -->

Plan de construcción etapa por etapa. Cada etapa es independientemente verificable antes de avanzar a la siguiente. Marcá las tareas completadas con `- [x]`.

---

## Etapa 0 — Entorno y hola mundo

**Objetivo:** tener el toolchain funcionando y poder flashear el XIAO ESP32-S3 sin fricción.

**Hardware requerido:** XIAO ESP32-S3 conectado por USB-C a la PC.

### Tareas

- [x] Instalar VS Code + extensión PlatformIO IDE (PlatformIO Core 6.1.19 ya presente)
- [x] Crear proyecto PlatformIO nuevo en `D:\Claude\espToy`
- [x] Configurar `platformio.ini`:
  ```ini
  [env:seeed_xiao_esp32s3]
  platform = espressif32
  board = seeed_xiao_esp32s3
  framework = arduino
  monitor_speed = 115200
  ```
- [x] Escribir `main.cpp` con blink del LED integrado + `Serial.println` en el loop
- [x] Compilar sin errores (`pio run`) — RAM 5.6 %, Flash 7.6 %
- [x] Flashear (`pio run --target upload`) — COM5, USB nativo, 10 s, hash verificado
- [x] Abrir monitor serial y verificar salida — ticks 1/s, uptime OK, heap estable en 372660 bytes

**Sabés que está lista cuando:** el monitor serial muestra mensajes periódicos (ej. `"espToy boot OK — tick N"`) y el LED parpadea.

---

## Etapa 1 — Pantalla viva

**Objetivo:** confirmar la dirección I2C del OLED, inicializar U8g2 y medir framerate real.

**Hardware requerido:** OLED SSD1309 128x64 conectado — SDA en D4/GPIO5, SCL en D5/GPIO6.

### Tareas

- [x] Agregar librería U8g2 a `platformio.ini` (`lib_deps = olikraus/U8g2`) — instalada 2.36.18
- [x] Escribir sketch de scan I2C para confirmar dirección — **confirmado 0x3C**
- [x] Instanciar display: `U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE)` (pines I2C por defecto del XIAO)
- [x] Inicializar con `u8g2.begin()` y dibujar texto/rectángulo de prueba — inicializa OK
- [x] Implementar loop de animación simple (pelota que rebota) con timestamp
- [x] Medir y loguear FPS por serial — log 1/s con fps + heap
- [x] Ajustar para alcanzar ~30 fps — **31 fps estables medidos**, heap fijo en 368400 bytes

**Sabés que está lista cuando:** la pantalla muestra una animación fluida sostenida a ~30 fps logueados por serial.

---

## Etapa 2 — Motor de ojos

**Objetivo:** ojos robóticos paramétricos que se vean "vivos" en idle.

**Hardware requerido:** el mismo de Etapa 1.

### Tareas

- [x] Definir struct `EyeParams` según doc 03 §1 — en `src/face.h/cpp`
- [x] Dibujar función de render con U8g2 (drawRBox + párpados como polígonos negros)
- [x] Implementar sistema de interpolación (ease-out `cur += (tgt-cur)*t`)
- [x] Implementar parpadeo automático: timer aleatorio 2-6 s, ~5 frames
- [x] Implementar mirada errante: offset aleatorio ±5 px cada 3-8 s + micro-movimiento senoidal
- [x] Definir expresiones como conjuntos de parámetros objetivo — **las 9 del doc 03** (NEUTRAL, FELIZ, TRISTE, ENOJADO, SORPRENDIDO, ABURRIDO, DORMIDO, SOSPECHOSO, AMOR)
- [x] Verificar que las transiciones entre expresiones no tienen saltos bruscos
- [x] Mantener ~30 fps con todo el motor activo — **31 fps medidos en hardware**

**Sabés que está lista cuando:** el personaje en idle se ve "vivo" (parpadea, mira, respira) y cambiar de expresión por código produce una transición fluida sin glitches.

---

## Etapa 3 — Entradas

**Objetivo:** botones y touch funcionando de forma confiable, cada uno disparando una reacción visual.

**Hardware requerido:** 2 botones en D0/GPIO1 y D1/GPIO2 (a GND), touch en D2/GPIO3.

### Tareas

- [x] Configurar GPIO1 y GPIO2 como `INPUT_PULLUP`
- [x] Implementar debounce por software (30 ms) para ambos botones
- [x] Leer touch con `touchRead(3)` + baseline al boot (50 lecturas, descarte de outliers) — baseline medida: 38063
- [x] Implementar detección de toque: **valor > baseline × 1.15** (en el S3 el valor SUBE al tocar, corregido del plan original) con 3 lecturas de confirmación e histéresis para soltar
- [x] Mapear entradas a reacciones (+ overlay en pantalla con el nombre del pin, para identificar botones físicos):
  - [x] Botón A (D0/GPIO1) → `SORPRENDIDO` 1.5 s
  - [x] Botón B (D1/GPIO2) → `FELIZ` 1.5 s
  - [x] Touch (D2/GPIO3) → `AMOR` mientras dure la caricia (mín. 2 s)
- [x] Loguear cada evento por serial para verificar detección
- [ ] Verificar que no hay doble-disparo ni pérdida de eventos bajo pulsación rápida — **pendiente: prueba física del usuario**

**Sabés que está lista cuando:** cada entrada dispara su reacción de forma confiable en 10 pruebas consecutivas sin falsos positivos ni rebotes.

---

## Etapa 4 — Sonido

**Objetivo:** buzzer pasivo emitiendo bips y melodías cortas sincronizados con eventos.

**Hardware requerido:** buzzer pasivo en D3/GPIO4. Si el buzzer es activo (no pasivo), ver Riesgos.

### Tareas

- [ ] Configurar LEDC en GPIO4: canal 0, resolución 8 bits
- [ ] Implementar función `tone(freq, durationMs)` usando `ledcWriteTone` y timer no bloqueante
- [ ] Implementar función `noTone()` para cortar el sonido
- [ ] Definir tabla de sonidos por evento:
  - [ ] Boot: secuencia ascendente corta (do-mi-sol)
  - [ ] Botón A: bip agudo corto
  - [ ] Botón B: bip medio doble
  - [ ] Touch/caricia: trino suave (modulación rápida)
  - [ ] Feliz: melodía de 3 notas ascendentes
  - [ ] Triste: nota descendente lenta
- [ ] Verificar que el sonido es no bloqueante (no interrumpe la animación)
- [ ] Agregar flag global `bool soundEnabled = true` para mute fácil

**Sabés que está lista cuando:** cada evento produce su sonido distintivo sin interrumpir la animación en pantalla. Si el buzzer no está disponible, setear `soundEnabled = false` y avanzar.

---

## Etapa 5 — Cerebro Tamagotchi

**Objetivo:** variables de humor con vida propia, persistidas entre reinicios.

**Hardware requerido:** el mismo de Etapas 1-4 (sin hardware nuevo).

### Tareas

- [ ] Definir struct `MoodState` con tres variables `uint8_t` (0-100):
  - `happiness` — felicidad general
  - `energy` — energía/hambre
  - `boredom` — aburrimiento
- [ ] Implementar decaimiento temporal con timer (cada 60 s en producción, configurable a 5 s para pruebas):
  - `energy` baja 2 pts/min
  - `happiness` baja 1 pt/min (más rápido si `boredom` > 70)
  - `boredom` sube 3 pts/min si no hay interacción
- [ ] Mapear entradas a modificaciones de humor:
  - Botón A: `+10 happiness`, `-5 boredom`
  - Botón B: `+15 energy`, `-10 boredom`
  - Touch: `+20 happiness`, `-15 boredom`
- [ ] Implementar función `moodToExpression()` que elige la expresión idle según la tabla de prioridad del doc 03 §5 (dormido / triste / enojado / aburrido / feliz / neutral)
- [ ] Inicializar NVS con `Preferences` de Arduino ESP32
- [ ] Guardar `MoodState` en NVS: timer de 5 min con dirty flag + guardado inmediato tras interacciones importantes (caricia, partida de Pong)
- [ ] Guardar `lastSavedEpoch` (hora del último guardado) junto al `MoodState`
- [ ] Cargar desde NVS al boot; si no existe, inicializar con valores default (50/80/0)
- [ ] **Decaimiento offline**: al boot con hora válida, calcular el tiempo apagado y aplicar el decaimiento equivalente, con tope de 48 h (si no hay hora válida, omitir — el apagado actúa como pausa). *Nota: requiere la hora de Etapa 6; dejar la función lista con guarda y activarla al completar esa etapa.*
- [ ] Loguear estado de humor por serial en cada cambio

**Sabés que está lista cuando:** apagás el dispositivo con un humor determinado, lo encendés, y el personaje arranca con la misma expresión que tenía. El humor cambia solo con el paso del tiempo. (Con Etapa 6 completa: si estuvo apagado "un día" — simulado con hora forzada — arranca notablemente más aburrido.)

---

## Etapa 6 — Ciclo día/noche

**Objetivo:** sincronizar con hora real vía NTP y que el personaje duerma de noche.

**Hardware requerido:** el mismo + acceso a WiFi con credenciales configurables.

### Tareas

- [ ] Agregar credenciales WiFi en defines o archivo de configuración (nunca hardcodeadas en el repo)
- [ ] Implementar conexión WiFi con timeout de 10 s y fallback a modo sin hora
- [ ] **Portal cautivo de configuración**: si no hay credenciales o la conexión falla, levantar AP `espToy-setup` con página web servida por el ESP32 para elegir red y clave desde el teléfono (guardar en NVS)
- [ ] Botón "usar la hora de este teléfono" en el portal: el navegador envía su hora local + zona → setea el RTC sin internet
- [ ] Activar el decaimiento offline de Etapa 5 (ya con hora válida disponible)
- [ ] Sincronizar hora con NTP (`pool.ntp.org`) usando `configTime` de Arduino
- [ ] Definir rango nocturno: 22:00-07:00 (configurable como constantes)
- [ ] Implementar estado `SLEEPING`:
  - Expresión: ojos cerrados con ZZZ animadas
  - Sonido: ronquido suave periódico (si buzzer disponible)
  - Entradas ignoradas... excepto al despertar
- [ ] Al presionar cualquier botón durante el sueño: expresión `ANGRY` breve ("me despertaste") y luego vuelve a dormir
- [ ] Desconectar WiFi después de sincronizar para ahorrar energía
- [ ] Mostrar hora en la pantalla opcionalmente (esquina, fuente pequeña)
- [ ] Simular hora nocturna por serial para pruebas sin esperar la noche

**Sabés que está lista cuando:** con hora forzada a las 23:00 por serial, el personaje duerme y ronca. Al presionar un botón, se enoja brevemente y vuelve a dormir.

---

## Etapa 7 — Pong oculto

**Objetivo:** minijuego completo accesible por combo secreto, con IA y vuelta limpia al personaje.

**Hardware requerido:** el mismo de Etapas 1-4 (botones A y B necesarios para jugar).

### Tareas

- [ ] Implementar detector de combo: botón A + botón B sostenidos simultáneamente durante 3 s
- [ ] Crear módulo `pong.cpp` / `pong.h` separado del motor principal
- [ ] Implementar campo de juego según doc 04 §2: área 128×55 px con marcador arriba, paletas 3×12 px, pelota de radio 2 px
- [ ] Paleta izquierda (jugador): controlada por botón A (arriba) y B (abajo)
- [ ] Paleta derecha (CPU): sigue la Y de la pelota con velocidad limitada (dificultad ajustable)
- [ ] Física de la pelota: velocidad aumenta levemente con cada rebote en paleta
- [ ] Marcador: primero en llegar a 5 gana; mostrado en la pantalla durante el juego
- [ ] Al terminar la partida (narrativa doc 04 §5: la mascota ES la CPU):
  - [ ] Jugador gana → la mascota se enoja / hace puchero + jingle "decepcionado"
  - [ ] CPU gana → la mascota festeja burlona + fanfarria corta
  - [ ] En ambos casos: `aburrimiento -= 40`
- [ ] Presionar ambos botones durante el juego → salir al menú principal (personaje)
- [ ] Verificar que el estado de humor se preserva durante y después del juego

**Sabés que está lista cuando:** una partida completa a 5 puntos es jugable sin bugs, la IA presenta resistencia razonable, y la vuelta al personaje es inmediata y limpia.

---

## Etapa 8 — Integración y pulido

**Objetivo:** el sistema completo corre de forma estable durante horas; últimos ajustes de feel.

**Hardware requerido:** sistema completo ensamblado (sin carcasa todavía).

### Tareas

- [ ] Correr el sistema completo por 4 horas sin intervención y verificar:
  - [ ] Sin crashes ni reinicios (watchdog log por serial)
  - [ ] Sin memory leaks (`ESP.getFreeHeap()` estable)
  - [ ] Pantalla sin glitches de renderizado
- [ ] Revisar y ajustar tiempos de animación:
  - [ ] Velocidad de interpolación (¿se siente natural?)
  - [ ] Duración de expresiones temporales (¿suficiente sin ser molesto?)
  - [ ] Frecuencia de parpadeo y mirada errante
- [ ] Ajustar curvas de decaimiento del humor según feel real
- [ ] Agregar al menos 1 easter egg (ej. secuencia de botones oculta → expresión especial)
- [ ] Revisar consumo de NVS: verificar que no se escriben más de ~1000 veces/día
- [ ] Documentar en código todos los pines, constantes y magic numbers
- [ ] Limpiar dead code y comentarios de debug

**Sabés que está lista cuando:** el sistema corre 4 horas sin intervención, `getFreeHeap()` se mantiene estable, y el comportamiento se siente pulido.

---

## Etapa 9 — Cuerpo 3D

**Objetivo:** diseñar e imprimir la carcasa física que contenga todos los componentes.

**Hardware requerido:** sistema completo funcionando (Etapas 0-8 completas).

### Tareas de diseño

- [ ] Definir dimensiones internas mínimas según PCB del XIAO ESP32-S3 y posición de componentes
- [ ] Diseñar ventana OLED: recorte para el área visible del panel 1.54" (~28×28 mm — medir el módulo real) + margen 1 mm, profundidad rasante con la pantalla
- [ ] Diseñar posición de botones: accesibles desde el exterior, con guías de alineación
- [ ] Diseñar zona de touch: pared de 1-2 mm de espesor máximo sobre el pad GPIO3 (paredes más gruesas atenúan la señal)
- [ ] Diseñar rejilla de ventilación/resonancia para el buzzer (agujeros pequeños en array)
- [ ] Diseñar acceso USB-C: recorte para el conector del XIAO, con tolerancia 0.5 mm
- [ ] Imprimir prototipo en PLA o PETG, espesor de pared 1.5-2 mm
- [ ] Montar componentes en el prototipo

### Tareas de recalibración post-montaje

- [ ] Recalibrar umbral touch con la carcasa puesta (la pared atenúa la señal; ajustar threshold)
- [ ] Verificar ángulo de visión del OLED con la ventana
- [ ] Verificar que los botones no rozan ni se quedan trabados
- [ ] Auditar cableado interno por vibraciones

**Sabés que está lista cuando:** el dispositivo está completamente ensamblado, todos los componentes son accesibles o visibles según corresponde, y el touch funciona con la carcasa puesta.

---

## Fase v2 — Backlog (sesiones futuras)

No empezar hasta completar Etapa 8. Estas funcionalidades requieren Etapas 0-8 estables.

- [ ] **IMU MPU6050** — conectar por I2C (dirección 0x68), leer acelerómetro:
  - Sacudida fuerte → expresión `SURPRISED` + sonido de susto
  - Inclinación sostenida → expresión `SAD` (mareado)
  - Detección de "panza arriba" → expresión de protesta
- [ ] **Space Invaders** — segundo minijuego, segundo combo secreto (ej. touch + botón A 3 s):
  - Oleadas de sprites enemigos 8x8
  - Botón A = moverse, botón B = disparar (o tap touch)
  - Dificultad incremental por nivel
- [ ] **Control por Bluetooth (BLE)** — el ESP32-S3 lo trae integrado; app o página Web Bluetooth (Chrome/Edge en Android y desktop) sin nada que instalar:
  - [ ] Servicio BLE con característica de estado: ver en vivo felicidad/energía/aburrimiento desde el teléfono ("¿cómo está?")
  - [ ] Interacción remota: mandarle un mimo, un mensaje o despertarlo desde el teléfono
  - [ ] **Editor de caras**: como las expresiones son sets de parámetros (`EyeParams`, doc 03), el teléfono puede mandar caras nuevas o modificadas por BLE; se guardan en NVS como slots de expresión custom (ej. 4 slots que reemplazan o suman al catálogo base)
  - [ ] Configuración remota: horario de sueño, volumen/mute, nombre del personaje
  - [ ] Nota iPhone: Safari no soporta Web Bluetooth; para iOS haría falta una app (ej. con Flutter/React Native) o usar una app genérica BLE tipo nRF Connect
- [ ] **Batería LiPo** — agregar TP4056 o equivalente; implementar lectura de nivel de batería por ADC; mostrar indicador en pantalla; modo de bajo consumo si batería < 20%
- [ ] **Más expresiones** — al menos 3 expresiones adicionales:
  - `LOVE` — iris en corazón, párpados caídos
  - `CONFUSED` — un ojo más abierto que el otro, mirada errante más errática
  - `EXCITED` — ojos muy abiertos, parpadeo rápido, "vibracion" de la cara

---

## Riesgos y planes B

### Dirección I2C distinta (0x3D en lugar de 0x3C)

**Síntoma:** el display no inicializa, `u8g2.begin()` retorna `false` o la pantalla queda negra.

**Plan B:** correr el sketch de scan I2C al inicio de Etapa 1. Si reporta `0x3D`, cambiar la constante de dirección en el constructor U8g2. Algunos módulos SSD1309 traen el jumper de dirección soldado en la posición alternativa.

### Imagen desplazada o con ruido visual (constructor incorrecto)

**Síntoma:** el contenido aparece cortado, desplazado o con franjas, pese a que el display inicializa.

**Plan B:** probar con `U8G2_SSD1309_128X64_NONAME2_F_HW_I2C`. La diferencia entre `NONAME0` y `NONAME2` está en el offset interno del controlador. Si tampoco funciona, probar `U8G2_SSD1309_128X64_NONAME_F_HW_I2C` sin sufijo numérico.

### Umbral touch inestable (falsos positivos o no detecta)

**Síntoma:** la expresión de caricia se dispara sola, o nunca responde al toque.

**Plan B 1 — Promediar baseline:** en lugar de 10 lecturas al boot, usar 50 lecturas y descartar outliers (eliminar el 10% mayor y menor antes de promediar).

**Plan B 2 — Histéresis:** requerir que el valor cruce el umbral de bajada Y suba de vuelta antes de aceptar el próximo toque (evita rebotes capacitivos).

**Plan B 3 — Ajuste físico:** si la carcasa atenúa demasiado, bajar el threshold relativo de 0.7 a 0.5. Si hay interferencia eléctrica, agregar un capacitor de 100 nF entre el pad y GND.

### Buzzer activo (no pasivo) conectado por error

**Síntoma:** el buzzer emite sonido constante o un único tono independientemente del PWM; `ledcWriteTone` no produce variación de pitch.

**Plan B:** los buzzers activos solo responden a on/off. Si no es posible reemplazarlo antes de continuar, setear `soundEnabled = false` en el código y seguir con las demás etapas. El módulo de sonido está diseñado con ese flag para este caso. Reemplazar por buzzer pasivo (sin generador interno) cuando esté disponible.

### WiFi no disponible o credenciales cambian

**Síntoma:** la conexión NTP falla en Etapa 6.

**Plan B:** el código de Etapa 6 tiene un timeout de 10 s y fallback a modo sin hora (el personaje nunca duerme, hora se muestra como `--:--`). Esto permite que todas las demás funcionalidades operen normalmente. Además, el portal cautivo permite setear la hora directamente desde el navegador del teléfono, sin internet ni router. Para pruebas de la lógica día/noche, usar la inyección de hora simulada por serial. Plan B de hardware para independencia total: módulo RTC DS3231 con pila (~USD 2, mismo bus I2C) en v2.
