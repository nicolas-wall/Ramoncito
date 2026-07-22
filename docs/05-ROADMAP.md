# Ramoncito — Roadmap de construcción
<!-- Documento 5 de la serie de planificación de Ramoncito -->

Plan de construcción etapa por etapa. Cada etapa es independientemente verificable antes de avanzar a la siguiente. Marcá las tareas completadas con `- [x]`.

---

## Etapa 0 — Entorno y hola mundo

**Objetivo:** tener el toolchain funcionando y poder flashear el XIAO ESP32-S3 sin fricción.

**Hardware requerido:** XIAO ESP32-S3 conectado por USB-C a la PC.

### Tareas

- [x] Instalar VS Code + extensión PlatformIO IDE (PlatformIO Core 6.1.19 ya presente)
- [x] Crear proyecto PlatformIO nuevo en `D:\Claude\Ramoncito`
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

**Sabés que está lista cuando:** el monitor serial muestra mensajes periódicos (ej. `"Ramoncito boot OK — tick N"`) y el LED parpadea.

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
- [x] Verificar que no hay doble-disparo ni pérdida de eventos bajo pulsación rápida — confirmado por el usuario, botones identificados

**Sabés que está lista cuando:** cada entrada dispara su reacción de forma confiable en 10 pruebas consecutivas sin falsos positivos ni rebotes.

---

## Etapa 4 — Sonido

**Objetivo:** buzzer pasivo emitiendo bips y melodías cortas sincronizados con eventos.

**Hardware requerido:** buzzer pasivo en D3/GPIO4. Si el buzzer es activo (no pasivo), ver Riesgos.

### Tareas

- [x] Configurar LEDC en GPIO4: canal 0, resolución 8 bits — `src/sound.cpp`
- [x] Implementar reproducción no bloqueante con `ledcWriteTone` (melodías como arrays {hz,ms})
- [x] Implementar `stop()` para cortar el sonido
- [x] Definir tabla de sonidos por evento — 10 melodías: BOOT, FELIZ, TRISTE, ENOJADO, AMOR, SORPRESA, DORMIR, DESPERTAR, RONQUIDO, BIP
- [x] Verificar que el sonido es no bloqueante (31 fps constantes con play() activo)
- [x] Flag de mute: `sound.setEnabled()` + comando serial `s`; toggle desde menú p3 (long-press)
- [x] **Sonido verificado en hardware** — buzzer pasivo funcionando con las melodías definidas

**Sabés que está lista cuando:** cada evento produce su sonido distintivo sin interrumpir la animación en pantalla. Si el buzzer no está disponible, setear `soundEnabled = false` y avanzar.

---

## Etapa 5 — Cerebro Tamagotchi

**Objetivo:** variables de humor con vida propia, persistidas entre reinicios.

**Hardware requerido:** el mismo de Etapas 1-4 (sin hardware nuevo).

### Tareas

- [x] Definir estado de humor: happiness/energy/boredom `uint8_t` 0-100 — `src/mood.cpp`
- [x] Implementar decaimiento temporal (verificado en hardware: -2 energía, -1/-2 felicidad, +3 aburrimiento por minuto; TIME_SCALE en config.h para acelerar pruebas)
- [x] Mapear entradas a modificaciones de humor (A: +10F -5A; B: +15E -10A; caricia: +20F -15A; + efectos de Pong y despertar nocturno ya definidos)
- [x] `dominantExpression()` según tabla doc 03 §5 (con orden corregido: la condición compuesta ENOJADO se evalúa antes que TRISTE)
- [x] NVS con `Preferences` (namespace "esptoy")
- [x] Guardar: timer 5 min con dirty flag + inmediato tras interacciones
- [x] Guardar `lastEpoch` junto al estado
- [x] Cargar al boot con defaults 50/80/0 — **verificado: el humor sobrevive al reinicio**
- [x] Decaimiento offline implementado con tope 48 h (se activa automáticamente cuando llega hora válida vía `justGotValidTime()`)
- [x] Log de humor por serial en cada cambio + comando de test `m F E A`

**Sabés que está lista cuando:** apagás el dispositivo con un humor determinado, lo encendés, y el personaje arranca con la misma expresión que tenía. El humor cambia solo con el paso del tiempo. (Con Etapa 6 completa: si estuvo apagado "un día" — simulado con hora forzada — arranca notablemente más aburrido.)

---

## Etapa 6 — Ciclo día/noche

**Objetivo:** sincronizar con hora real vía NTP y que el personaje duerma de noche.

**Hardware requerido:** el mismo + acceso a WiFi con credenciales configurables.

### Tareas

- [x] Credenciales solo en NVS vía portal (nada hardcodeado; `secrets.h` quedó como convención opcional sin uso)
- [x] Conexión WiFi con timeout de 10 s y fallback (a portal / modo sin hora) — `src/net.cpp`
- [x] **Portal cautivo**: AP `Ramoncito-setup` (192.168.4.1) con página para elegir red y clave desde el teléfono — verificado activo en hardware; indicador "WiFi: Ramoncito-setup" en pantalla
- [x] Botón "usar la hora de este teléfono" en el portal (`/settime` con epoch UTC del navegador + `settimeofday`)
- [x] Decaimiento offline conectado vía `justGotValidTime()` → `applyOfflineDecay()`
- [x] NTP con `configTime(TZ_OFFSET_S=UTC-3, pool.ntp.org)` + resync diario
- [x] Rango nocturno 22:00-07:00 en config.h
- [x] Estado `SLEEPING`: expresión DORMIDO con Zzz, ronquido periódico (2.5-3.5 s), touch no lo despierta
- [x] Botón durante el sueño → `ENOJADO` 2.5 s ("GRRR... dejame dormir") + efecto de humor, y vuelve a dormir — verificado con hora forzada
- [x] `WiFi.disconnect()` + `WIFI_OFF` después de sincronizar
- [x] Simulación de hora por serial (`h 23` / `h -1`) — **verificado: duerme a las 23, despierta a las 8**
- [x] Prueba de NTP real — **conectado a la red del usuario y hora local sincronizada (16:55 12/07/2026)**; el portal se cierra solo al lograrlo. Reintentos AP_STA cada 30 s si la conexión falla
- [ ] (Opcional, descartado por ahora) Mostrar hora en pantalla

**Sabés que está lista cuando:** con hora forzada a las 23:00 por serial, el personaje duerme y ronca. Al presionar un botón, se enoja brevemente y vuelve a dormir.

---

## Etapa 7 — Pong oculto

**Objetivo:** minijuego completo accesible por combo secreto, con IA y vuelta limpia al personaje.

**Hardware requerido:** el mismo de Etapas 1-4 (botones A y B necesarios para jugar).

### Tareas

- [x] Implementar detector de combo A+B 3 s (`InputEvent::COMBO_AB_3S` en input.cpp, emisión única con rearme al soltar ambos)
- [x] Crear módulo `pong.cpp` / `pong.h` separado del motor principal
- [x] Campo según doc 04 §2: área 128×55, marcador arriba, paletas 3×12, pelota radio 2, línea central punteada
- [x] Paleta jugador: botón A sube, botón B baja (estado mantenido)
- [x] IA de la CPU con error aleatorio decayente y tabla de dificultad progresiva (1.8→3.0 px/f, error 10→2)
- [x] Física: ángulo por punto de impacto ±60°, +6 %/rebote con tope 6.0, anti-doble-rebote
- [x] Marcador a 5 con pantalla de resultado 2 s ("GANASTE 5-3" / "GANE YO!")
- [x] Fin de partida (la mascota ES la CPU) — **verificado en hardware con partida completa**:
  - [x] Jugador gana → puchero/enojo + jingle triste
  - [x] CPU gana → festeja + fanfarria — `[mood] JUGO_PONG_GANO_CPU -> F:100 A:0` en el log
  - [x] En ambos casos: `aburrimiento -= 40`
- [x] Combo A+B durante el juego → abandona y vuelve al personaje
- [x] El humor se preserva durante el juego y recibe el efecto al final
- [ ] Playtest humano: partida real con botones (dificultad ganable, combo cómodo) — pendiente del usuario

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

## Novedades recientes (post-Etapa 8)

- [x] **OTA por GitHub Releases** — chequeo automático en boot + cada 24 h; confirmación táctil en menú p3; descarga e instala sobre HTTPS. (`src/ota.cpp`, `src/net.cpp`)
- [x] **IMU MPU6050** — sacudida (leve → SORPRENDIDO, excesiva → MAREADO + malhumor), levantado por orientación (→ ILUSIONADO), reacciones nocturnas. (`src/imu.cpp`) **Implementado y compilado; verificación en hardware pendiente.**
- [x] **Eliminación de Pong** — código muerto eliminado; el combo A+B ya no existe. (`src/pong.cpp/.h` borrados)
- [x] **Renacer** — doble confirmación táctil desde menú p2 (cabeza → pantalla de aviso, pie → confirma); resetea personalidad + humor + fecha de nacimiento a cero; dispara animación de nacimiento. (`main.cpp`, `personality.cpp`, `mood.cpp`)
- [x] **Animación de nacimiento** — secuencia 3 fases ~2.2 s: DORMIDO → pop SORPRENDIDO → FELIZ; se dispara al renacer y la primera vez que llega hora NTP si el juguete no tenía fecha de nacimiento.

## Fase v2 — Backlog (sesiones futuras)

No empezar hasta completar Etapa 8. Estas funcionalidades requieren Etapas 0-8 estables.

- [ ] **IMU MPU6050** (backlog v2 original — ya implementado, ver arriba)
- [x] Sacudida, levantado, reacción nocturna — implementados
- [ ] **Space Invaders** — segundo minijuego, segundo combo secreto (ej. touch + botón A 3 s):
  - Oleadas de sprites enemigos 8x8
  - Botón A = moverse, botón B = disparar (o tap touch)
  - Dificultad incremental por nivel
- [ ] **NFC** — lector PN532 por I2C (mismo bus del OLED, ~USD 4) o tags NTAG21x baratos: acercarle "objetos" físicos que el toy reconoce (comida de juguete, un corazón impreso en 3D con tag adentro) y reacciona distinto según cuál sea
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
