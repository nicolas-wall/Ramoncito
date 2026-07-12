# espToy — Plan Maestro

> Documento 0 de la serie de planificación. Punto de entrada: qué es el proyecto, qué se decidió y dónde está cada especificación. Actualizado: 2026-07-12.

---

## 1. Visión

**espToy** es una mascota virtual física tipo Tamagotchi: un personaje cuya cara vive en una pantalla OLED, con personalidad propia que evoluciona con el tiempo, que reacciona a botones y caricias, duerme de noche, hace sonidos… y esconde un Pong si conocés el secreto. La electrónica se monta después dentro de un cuerpo impreso en 3D — la pantalla es la cara, el cuerpo le da forma.

Lo que lo hace sentirse "vivo" no es una función puntual sino la suma: parpadeo con timing aleatorio, mirada que deambula, transiciones suaves entre emociones, humor que cambia solo, y sonido expresivo. Por eso la regla número uno del firmware es que **nada bloquee jamás el loop de animación**.

## 2. Serie de documentos

| Doc | Archivo | Contenido |
|---|---|---|
| 0 | `00-PLAN-MAESTRO.md` | Este documento: visión, decisiones, índice |
| 1 | `01-HARDWARE.md` | BOM, diagrama de conexiones, pinout, detalle por componente, cuerpo 3D |
| 2 | `02-ARQUITECTURA.md` | Módulos del firmware, máquina de estados, loop principal, contratos |
| 3 | `03-EXPRESIONES.md` | Motor de ojos paramétricos, catálogo de expresiones, animaciones idle |
| 4 | `04-PONG.md` | Minijuego oculto: entrada secreta, física, IA, integración con el personaje |
| 5 | `05-ROADMAP.md` | Construcción paso a paso en 10 etapas con checkboxes y criterios de aceptación |

Orden de lectura para construir: 5 (qué sigue) → el doc de la etapa en curso. El roadmap referencia a los demás.

## 3. Decisiones cerradas

| Tema | Decisión |
|---|---|
| Placa | Seeed Studio XIAO ESP32-S3 **básico** (sin Sense) |
| Pantalla | OLED 1.54" 128×64, driver **SSD1309** confirmado por serigrafía, **I2C** (0x3C probable) |
| Librería gráfica | U8g2, constructor `U8G2_SSD1309_128X64_NONAME0_F_HW_I2C` |
| Entorno | **PlatformIO** + framework Arduino, board `seeed_xiao_esp32s3` |
| Personalidad | **Tipo Tamagotchi**: felicidad/energía/aburrimiento (0-100) con decaimiento, persistidas en NVS. **Decaimiento offline**: al reencender aplica el tiempo que estuvo apagado (tope 48 h) — te "extraña" si lo abandonás |
| Entradas | 2 botones (D0/D1) + **touch capacitivo** (D2, cable/cinta de cobre a través del cuerpo 3D). **Roles (decidido en uso real)**: la caricia es LA interacción afectiva; los botones son utilitarios — cualquiera abre el **menú de estado** (hora, WiFi, barras de humor) y el combo A+B 3 s queda reservado para el juego oculto |
| Sonido | **Buzzer pasivo** en D3 vía PWM LEDC (único componente a comprar, ~USD 1) |
| Conectividad | WiFi **solo para NTP** al boot → ciclo día/noche (22:00–07:00 duerme). Configuración **desde el teléfono** vía portal cautivo (el toy levanta su propia red "espToy-setup"); el portal también permite tomar la hora del navegador del teléfono, sin internet |
| Minijuego | **Pong** vs CPU, oculto tras combo A+B 3 s; la mascota "es" la CPU y reacciona al resultado |
| Estética | Ojos robot procedurales (estilo RoboEyes/Emo), sin bitmaps, interpolación entre expresiones |
| Diferido a v2 | IMU MPU6050, Space Invaders, batería LiPo, expresiones extra, **NFC** (lector PN532 I2C o tags NTAG: objetos físicos que el toy "reconoce"), **BLE**: ver estado desde el teléfono, interacción remota y editor de caras (subir expresiones nuevas como parámetros, guardadas en NVS) |

Pinout completo y justificado: doc 01 §3. Resumen: botones D0/D1, touch D2, buzzer D3, OLED D4 (SDA) / D5 (SCL).

## 4. Alcance por versión

- **v1 — el personaje vive**: cara con ~9 expresiones e idle animado, reacciones a botones/caricia, humor persistente, sonido, sueño nocturno por NTP, Pong oculto. *(Etapas 0-8 del roadmap)*
- **v1 física**: cuerpo 3D con ventana OLED, botones accesibles, pared fina para el touch, rejilla del buzzer. *(Etapa 9)*
- **v1.5 — pulido**: animaciones raras (bostezo, mirada fija, sacudida), easter eggs, corazón flotante en mimo.
- **v2 — backlog**: IMU (reacciona a sacudidas/inclinación), Space Invaders, batería LiPo con indicador, y **BLE con el teléfono**: ver el estado en vivo, interactuar a distancia y subir/editar caras (los sets de parámetros del doc 03 viajan por BLE y se guardan en NVS).

## 5. Estado actual y próximo paso

- [x] Análisis y decisiones de diseño
- [x] Serie de documentos de planificación (este conjunto)
- [ ] **Próximo paso: Etapa 0 del roadmap** — crear el proyecto PlatformIO y flashear el hola mundo
- [ ] Comprar el buzzer pasivo (no bloquea: las etapas 0-3 no lo necesitan y el módulo de sonido tiene flag de mute)

## 6. Riesgos principales

Los cinco riesgos identificados (dirección I2C 0x3D, constructor NONAME2, touch inestable, buzzer activo por error, WiFi ausente) tienen plan B documentado en doc 05 §Riesgos. Ninguno es estructural: todos se resuelven con un cambio de constante o un flag.
