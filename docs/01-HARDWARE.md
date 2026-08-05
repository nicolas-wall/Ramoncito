# 01 — Hardware del Ramoncito

> Documento 1 de la serie de planificación de Ramoncito — **mini arcade** con pantalla OLED, palanca y botones, en cuerpo impreso en 3D, basado en Seeed Studio XIAO ESP32-S3.
>
> **Cambio de rumbo (v0.10):** el proyecto arrancó como mascota tipo Tamagotchi con dos sensores táctiles (caricia y cosquillas). Ahora es un mini arcade: los dos pines de touch pasaron a la palanca y a un botón, y la mascota queda como cara de reposo del gabinete. Lo que sigue describe el hardware **del arcade**.

---

## 1. Lista de materiales (BOM)

| Componente | Cantidad | Estado | Costo aprox. |
|---|---|---|---|
| Seeed Studio XIAO ESP32-S3 (básico, no Sense) | 1 | Tengo | — |
| OLED 1.54" 128×64 I2C (SSD1309) | 1 | Tengo | — |
| Pulsador / botón táctil momentáneo | 2–3 | Tengo | — |
| Buzzer pasivo (tipo KY-006 o equivalente de kit Arduino) | 1 | Tengo | — |
| IMU MPU6050 (GY-521) | 1 | Tengo | — |
| Cables y protoboard | — | Tengo | — |
| **Palanca analógica KY-023** (thumbstick tipo PS2, 2 ejes + pulsador) | 1 | **A comprar** | ~USD 1–2 |
| **Opcional:** botones arcade de 16 mm (tacto real) | 2–3 | Futuro | ~USD 0.5 c/u |
| **Opcional:** Batería LiPo 3.7 V (400–600 mAh) | 1 | Futuro | ~USD 5 |

Los dos tact switch que ya había alcanzan para jugar: la palanca es lo único que falta comprar.

---

## 2. Diagrama de conexiones

```
                    XIAO ESP32-S3
                   ┌─────────────┐
              GND ─┤ GND     3V3 ├─ VCC (OLED + palanca)
                   │             │
      Botón A ──── ┤ D0 (GPIO1)  │  ← ya cableado
   Palanca VRx ─── ┤ D1 (GPIO2)  │  (ADC1_CH1)
      Botón C ──── ┤ D2 (GPIO3)  │  ← ya cableado
                   │             │
   Buzzer (+) ──── ┤ D3 (GPIO4) ─┤── [~100Ω] ──► Buzzer ──► GND
                   │             │
      OLED SDA ─── ┤ D4 (GPIO5)  │
      OLED SCL ─── ┤ D5 (GPIO6)  │
                   │             │
      Botón B ──── ┤ D8 (GPIO7)  │  ← ya cableado
   Palanca VRy ─── ┤ D9 (GPIO8)  │  (ADC1_CH7)
   Palanca SW ──── ┤ D10 (GPIO9) │
                   │             │
                   │    USB-C    │ ◄── alimentación / programación
                   └─────────────┘

Botones (los tres iguales):
  [BTN] ── pin del XIAO
  [BTN] ── GND
  Pull-up interno por software (INPUT_PULLUP), sin resistencia externa.
  Un pin sin botón cableado queda en HIGH = suelto, así que se pueden
  montar de a uno sin tocar el firmware.

OLED 4 pines:
  GND ── GND del XIAO
  VCC ── 3V3 del XIAO
  SCL ── D5 (GPIO6)
  SDA ── D4 (GPIO5)

Palanca KY-023 (5 pines):
  GND ── GND del XIAO
  +5V ── 3V3 del XIAO   ← 3.3 V, NO 5 V (ver §4.3)
  VRx ── D1  (GPIO2)
  VRy ── D9  (GPIO8)
  SW  ── D10 (GPIO9)
```

---

## 3. Tabla de pinout definitivo

| Componente | Pin XIAO | GPIO real | Nota |
|---|---|---|---|
| Botón A | D0 | GPIO1 | a GND, `INPUT_PULLUP` interno, sin resistencia externa. **Cableado y verificado** |
| Palanca eje X | D1 | GPIO2 | analógico, **ADC1**_CH1. Junta verificada |
| Botón C / activar | D2 | GPIO3 | a GND, `INPUT_PULLUP`. **Cableado y verificado.** Pin de strapping: ver nota abajo |
| Buzzer pasivo | D3 | GPIO4 | PWM LEDC, resistencia ~100 Ω en serie recomendada |
| OLED SDA | D4 | GPIO5 | I2C por defecto del XIAO |
| OLED SCL | D5 | GPIO6 | I2C por defecto del XIAO |
| Botón B / cursor | D8 | GPIO7 | a GND, `INPUT_PULLUP`. **Cableado y verificado** |
| Palanca eje Y | D9 | GPIO8 | analógico, **ADC1**_CH7 |
| Pulsador de la palanca | D10 | GPIO9 | a GND, `INPUT_PULLUP`. ⚠ **Soldadura abierta en la unidad de dev** |

**Sobre D10 (GPIO9):** en la unidad de desarrollo este pin dejó de conducir. No responde ni con un puente directo contra GND, así que la falla está en la junta y no en el micro —el pin llegó a funcionar antes, con 14 pulsaciones limpias, y dejó de hacerlo tras un intento de resoldado—. Por eso se le asignó el pulsador de la palanca, que es la función más prescindible: duplica al botón C. Si el pin nunca revive, no se pierde nada. En una unidad sana, la asignación es válida igual.

**Por qué los ejes van sí o sí a ADC1:** el ESP32-S3 le presta el ADC2 al driver de WiFi. Con el WiFi levantado —que acá es siempre, por OTA, panel LAN y Telegram— las lecturas de ADC2 fallan o devuelven basura. ADC1 son los GPIO1 a GPIO10, y ahí están GPIO2 y GPIO8.

**Los dos pulsadores que ya estaban montados van a GPIO1 y GPIO3**, verificado con el escáner de pines (comando serial `k`, ver §4.5). El segundo quedó mapeado como botón C (activar) y no como B porque con solo dos botones "avanzar de página + activar la opción" es una combinación usable, mientras que "avanzar de página + mover cursor" no dejaría confirmar nada.

**Por qué GPIO3 lleva un botón y no un eje:** GPIO3 es pin de strapping del ESP32-S3 (selección de JTAG). Con los eFuses de fábrica se ignora al bootear, así que es utilizable; pero un botón con pull-up queda en HIGH durante el arranque, que es un nivel lógico definido, mientras que un eje analógico en reposo queda a media tensión. Entre los dos, el botón es la opción segura.

---

## 4. Detalle por componente

### 4.1 OLED 1.54" 128×64 — SSD1309

El SSD1309 es la variante de 1.54" del clásico SSD1306: mismo protocolo I2C, mismo set de comandos, diferente tamaño de panel. La serigrafía de esta pantalla dice "OLED M154_4P, port: IIC, driver IC: SSD1309".

**Conexión:**

```
OLED  →  XIAO
GND   →  GND
VCC   →  3V3
SCL   →  D5 (GPIO6)
SDA   →  D4 (GPIO5)
```

Alimentación: 3.3 V (no conectar a 5 V).

**Librería:** U8g2.

Constructor a usar:
```cpp
U8G2_SSD1309_128X64_NONAME0_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);
```

Si la imagen aparece corrida o mal alineada verticalmente, cambiar `NONAME0` por `NONAME2`.

**Dirección I2C:** 0x3C (la más común). Fallback: 0x3D (en algunas unidades el pad ADDR está en HIGH). Para verificar, correr un scan I2C antes de arrancar el proyecto:

```cpp
Wire.begin();
for (byte addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0)
        Serial.printf("Dispositivo encontrado en 0x%02X\n", addr);
}
```

---

### 4.2 Botones A, B y C

Tres pulsadores momentáneos estándar (tipo tact switch). Los dos que ya había alcanzan para empezar; el tercero es opcional (START / coin).

**Conexión:** un terminal al pin GPIO correspondiente, el otro terminal a GND. Sin resistencia externa.

**Configuración en firmware:**
```cpp
pinMode(PIN_BTN_A,  INPUT_PULLUP);  // GPIO1
pinMode(PIN_BTN_B,  INPUT_PULLUP);  // GPIO7
pinMode(PIN_BTN_C,  INPUT_PULLUP);  // GPIO3
pinMode(PIN_JOY_SW, INPUT_PULLUP);  // GPIO9
```

Con `INPUT_PULLUP`, el pin lee HIGH en reposo y LOW cuando el botón está presionado. Un pin sin nada conectado también lee HIGH, así que los botones se pueden ir montando de a uno.

**Debounce:** por software, `DEBOUNCE_MS = 30`. No se necesita capacitor externo.

**Tacto:** los tact switch de 6 × 6 mm sirven para probar todo el firmware, pero se sienten a protoboard. Para tacto de arcade real, botones de 16 mm — los de 24 mm quedan enormes al lado de una pantalla de 1.54".

---

### 4.3 Palanca analógica (KY-023 / thumbstick tipo PS2)

Adentro son dos potenciómetros de ~10 kΩ montados en cruz, más un pulsador que se activa al apretar la palanca hacia abajo. Cada potenciómetro es un divisor de tensión: entrega de 0 V a VCC según la posición, con el reposo cerca de la mitad.

**Alimentar a 3.3 V, no a 5 V.** El módulo dice "+5V" en la serigrafía, pero no tiene electrónica activa: son resistencias, y funcionan igual a 3.3 V. Es más: *hay que* alimentarlo a 3.3 V, porque el ADC del ESP32-S3 no tolera entradas por encima de 3.3 V. A 5 V los extremos del recorrido meterían ~5 V en un pin del micro.

**Rango de lectura:** con `analogReadResolution(12)` y atenuación de 11 dB, el ADC devuelve 0..4095 sobre el rango completo de 3.3 V.

**Calibración del centro:** ninguna unidad reposa exactamente en 2048, y la diferencia varía entre ejes de la misma palanca. El firmware promedia `JOY_MUESTRAS_CALIB` lecturas al bootear y guarda ese valor como centro. **No hay que tocar la palanca durante el arranque.**

**Zona muerta:** los thumbsticks baratos no vuelven siempre al mismo punto. Sin zona muerta, un eje en reposo queda oscilando y el control deriva solo. `JOY_DEADZONE = 0.18` (18 % del recorrido) ignora ese ruido, y el valor se reescala para que apenas se sale de la zona muerta el control arranque en 0 y no pegue un salto.

**De eje analógico a dirección:** los juegos usan el valor continuo (`axisX()` / `axisY()`, −1 a +1) para control proporcional. Los menús usan eventos discretos, generados con histéresis: la dirección se activa al superar `JOY_UMBRAL_ON` (0.50) y no se suelta hasta bajar de `JOY_UMBRAL_OFF` (0.35). Sin histéresis, una palanca sostenida justo en el umbral dispara un tren de eventos. Mantenerla genera auto-repeat: primero espera `JOY_REPEAT_DELAY_MS`, después repite cada `JOY_REPEAT_MS`.

**Si un eje va al revés:** invertirlo por firmware con `JOY_INVERTIR_X` / `JOY_INVERTIR_Y` en `config.h`. No hace falta recablear ni tocar la lógica de los juegos.

**Tamaño para el gabinete:** PCB de 26 × 34 mm, palanca de ~20 mm de alto. Al lado del OLED de 1.54" (módulo de 42 × 38 mm) da una proporción de arcade correcta.

---

### 4.4 Buzzer pasivo

**Pasivo vs activo — diferencia crítica:**

| Tipo | Interno | Cómo suena | Sirve para melodías |
|---|---|---|---|
| Pasivo (piezo sin oscilador) | Sin circuito oscilador | Solo si se le envía una señal PWM de la frecuencia deseada | Sí |
| Activo (con oscilador integrado) | Oscilador interno fijo | Siempre el mismo tono cuando se conecta a VCC | No |

Para Ramoncito se necesita el **pasivo**, ya que el firmware genera las notas por PWM.

**Cómo distinguirlos físicamente:**
- **Pasivo:** suele tener la placa de circuito visible (color verde) en la cara inferior.
- **Activo:** cara inferior sellada con material negro.
- **Test definitivo:** aplicar 3.3 V directos entre + y −. El activo suena solo (tono constante). El pasivo hace un clic al conectar y al desconectar, pero no emite tono sostenido.

**Conexión:**

```
GPIO4 (D3) ──[100 Ω]──► (+) Buzzer (−) ──► GND
```

La resistencia de 100 Ω en serie limita la corriente y protege el pin. El buzzer típico de kit Arduino a 3.3 V no necesita transistor de potencia.

**Firmware:** se usa el módulo LEDC del ESP32-S3 para generar PWM a la frecuencia de la nota deseada (frecuencia varía; duty cycle fijo al 50%).

---

### 4.5 Escáner de pines — comando serial `k`

Para no tener que seguir un cable a ojo en la protoboard, el firmware trae un escáner. El comando `k` pone en `INPUT_PULLUP` todos los pines libres del XIAO e imprime el nivel de cada uno:

```
[scan] D0/g1:1 D1/g2:1 D2/g3:0 D8/g7:1 D9/g8:1 D10/g9:1 D6/g43:1 D7/g44:1
                          ↑ hay un botón acá, apretado
```

**Cómo usarlo:** mantener el botón apretado varios segundos y mandar `k` repetidamente. El pin que aparezca en `0` mientras se sostiene es donde está cableado. Un pin en `1` permanente está suelto o no tiene el otro terminal a GND.

Si la palanca está montada (`JOYSTICK_PRESENTE = true`), el escáner saltea sus dos ejes: ponerles un pull-up falsearía la lectura del ADC.

---

## 5. Alimentación

**Fase v1 (actual):** todo el circuito se alimenta por USB-C desde el XIAO. El ESP32-S3 regula internamente a 3.3 V.

**Consumo estimado en v1:**

| Componente | Consumo típico |
|---|---|
| XIAO ESP32-S3 (activo normal) | 40–80 mA |
| OLED 128×64 | 15–25 mA |
| Buzzer pasivo (activo) | 5–15 mA |
| Palanca (2 divisores de 10 kΩ a 3.3 V) | < 1 mA |
| Total estimado | < 100 mA |

Bien dentro del límite del regulador onboard del XIAO y de la corriente de un puerto USB estándar.

**Fase v2 (futura):** el XIAO ESP32-S3 básico incluye pads de batería LiPo y cargador integrado en la cara inferior. Para agregar batería solo se necesita soldar los pads + y − a una celda LiPo de 3.7 V. El cargador opera a 5 V desde el USB-C. Sin cambios en el firmware base.

---

## 6. Consideraciones para el cuerpo 3D

Al diseñar o modelar el cuerpo del Ramoncito, tener en cuenta los siguientes puntos de integración:

**OLED:**
- Ventana rectangular de 28 × 28 mm aprox. (o ajustar al tamaño real del panel activo) en la cara frontal.
- El módulo OLED se apoya desde atrás con tolerancia de 0.2–0.3 mm. Puede fijarse con pegamento UV o encastre de fricción.

**Botones A, B y C:**
- Agujeros o canales en el panel para que el émbolo de cada pulsador sea accesible desde el exterior.
- Separación mínima entre botones: 10–12 mm para que sean cómodos de presionar.
- Disposición de arcade: los botones a la derecha del panel, la palanca a la izquierda.

**Palanca:**
- Ventana para la base del módulo (PCB de 26 × 34 mm) y un agujero para el vástago, con holgura suficiente para el recorrido completo en las cuatro direcciones.
- El módulo se atornilla o se pega desde adentro; el vástago tiene que sobresalir del panel lo suficiente para agarrarlo con dos dedos.
- Dejar el panel con un ángulo de 10–20° (como un control de arcade real) hace la palanca mucho más cómoda que un panel plano.

**Buzzer:**
- El buzzer es omnidireccional pero gana volumen con una pequeña cámara acústica detrás del diafragma.
- Opción 1: rejilla de orificios (∅ 1–1.5 mm) en la zona de salida de sonido.
- Opción 2: cavidad cerrada detrás del buzzer con un único orificio de salida (tipo Helmholtz simple) para reforzar la frecuencia principal.
- El buzzer no debe quedar en contacto directo con paredes sólidas sin salida de aire: pierde volumen.

**USB-C:**
- Acceso al conector USB-C del XIAO para carga y reprogramación sin desensamblar.
- Un slot en la base o lateral del cuerpo de aprox. 10 × 5 mm es suficiente.
- Considerar también acceso al botón RESET del XIAO si se necesita durante el desarrollo.
