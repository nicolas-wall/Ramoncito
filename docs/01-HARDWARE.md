# 01 — Hardware del espToy

> Documento 1 de la serie de planificación de espToy — mascota virtual tipo Tamagotchi con cara OLED montada en cuerpo impreso en 3D, basada en Seeed Studio XIAO ESP32-S3.

---

## 1. Lista de materiales (BOM)

| Componente | Cantidad | Estado | Costo aprox. |
|---|---|---|---|
| Seeed Studio XIAO ESP32-S3 (básico, no Sense) | 1 | Tengo | — |
| OLED 1.54" 128×64 I2C (SSD1309) | 1 | Tengo | — |
| Pulsador / botón táctil momentáneo | 2 | Tengo | — |
| Cables y protoboard | — | Tengo | — |
| Buzzer pasivo (tipo KY-006 o equivalente de kit Arduino) | 1 | **A comprar** | ~USD 1 |
| **Opcional v2:** IMU MPU6050 | 1 | Futuro | ~USD 2 |
| **Opcional v2:** Batería LiPo 3.7 V (400–600 mAh) | 1 | Futuro | ~USD 5 |

---

## 2. Diagrama de conexiones

```
                    XIAO ESP32-S3
                   ┌─────────────┐
              GND ─┤ GND     3V3 ├─ VCC (OLED)
                   │             │
      Botón A ─── ┤ D0 (GPIO1)  │
      Botón B ─── ┤ D1 (GPIO2)  │
   Touch (cobre) ─┤ D2 (GPIO3)  │
                   │             │
   Buzzer (+) ─── ┤ D3 (GPIO4) ─┤── [~100Ω] ──► Buzzer ──► GND
                   │             │
      OLED SDA ── ┤ D4 (GPIO5)  │
      OLED SCL ── ┤ D5 (GPIO6)  │
                   │             │
                   │    USB-C    │ ◄── alimentación / programación
                   └─────────────┘

Botones:
  [BTN A] ── D0 (GPIO1) ──┐
  [BTN B] ── D1 (GPIO2) ──┘ (el otro terminal de cada botón va a GND)
  Pull-up interno habilitado por software (INPUT_PULLUP), sin resistencia externa.

OLED 4 pines:
  GND ── GND del XIAO
  VCC ── 3V3 del XIAO
  SCL ── D5 (GPIO6)
  SDA ── D4 (GPIO5)

Touch:
  Cinta/cable de cobre ── D2 (GPIO3)   (sensado capacitivo nativo del ESP32-S3)
```

---

## 3. Tabla de pinout definitivo

| Componente | Pin XIAO | GPIO real | Nota |
|---|---|---|---|
| Botón A | D0 | GPIO1 | a GND, `INPUT_PULLUP` interno, sin resistencia externa |
| Botón B | D1 | GPIO2 | ídem |
| Touch caricia | D2 | GPIO3 | cable/cinta de cobre, sensado capacitivo nativo ESP32-S3 |
| Buzzer pasivo | D3 | GPIO4 | PWM LEDC, resistencia ~100 Ω en serie recomendada |
| OLED SDA | D4 | GPIO5 | I2C por defecto del XIAO |
| OLED SCL | D5 | GPIO6 | I2C por defecto del XIAO |

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

### 4.2 Botones A y B

Dos pulsadores momentáneos estándar (tipo tact switch).

**Conexión:** un terminal al pin GPIO correspondiente, el otro terminal a GND. Sin resistencia externa.

**Configuración en firmware:**
```cpp
pinMode(GPIO1, INPUT_PULLUP);  // Botón A
pinMode(GPIO2, INPUT_PULLUP);  // Botón B
```

Con `INPUT_PULLUP`, el pin lee HIGH en reposo y LOW cuando el botón está presionado.

**Debounce:** por software, con un filtro de tiempo mínimo entre lecturas (típicamente 20–50 ms). No se necesita capacitor externo.

---

### 4.3 Touch capacitivo — caricia

El ESP32-S3 tiene sensado capacitivo nativo en varios pines. GPIO3 (D2 en el XIAO) es uno de los pines touch disponibles.

**Cómo funciona:** el ESP32-S3 mide el tiempo de carga de un capacitor interno conectado al pin. Cuando un dedo (o una superficie conductora conectada) se acerca, la capacitancia del pin aumenta y el valor leído cambia. La función de lectura es:

```cpp
uint32_t valor = touchRead(GPIO3);
```

**OJO — comportamiento del ESP32-S3** (verificado en hardware): a diferencia del ESP32 original (donde el valor cae al tocar), en el S3 `touchRead()` devuelve valores grandes (~38000 en este hardware) que **AUMENTAN al tocar**. La detección es `valor > baseline × 1.15`. El umbral exacto depende del hardware montado y hay que calibrarlo.

**Cómo armar el sensor:**

- Material conductor (cinta de cobre adhesiva, cinta de aluminio, o un cable en espiral) pegado por dentro del cuerpo 3D, en la zona de caricia.
- Un cable fino conecta ese parche conductor al pin D2 (GPIO3).
- El ESP32-S3 detecta la proximidad/contacto del dedo **a través del plástico**, siempre que la pared sea de 1 a 2 mm de grosor aproximadamente. Paredes más gruesas pueden requerir ajuste del umbral o reducir la sensibilidad hasta hacerlo inoperable.

**Calibración:** el umbral de detección varía según el área del parche, el grosor del plástico y la humedad ambiental. Se planea una autocalibración al boot: el firmware toma N muestras en reposo y calcula el umbral dinámicamente.

---

### 4.4 Buzzer pasivo

**Pasivo vs activo — diferencia crítica:**

| Tipo | Interno | Cómo suena | Sirve para melodías |
|---|---|---|---|
| Pasivo (piezo sin oscilador) | Sin circuito oscilador | Solo si se le envía una señal PWM de la frecuencia deseada | Sí |
| Activo (con oscilador integrado) | Oscilador interno fijo | Siempre el mismo tono cuando se conecta a VCC | No |

Para espToy se necesita el **pasivo**, ya que el firmware genera las notas por PWM.

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

## 5. Alimentación

**Fase v1 (actual):** todo el circuito se alimenta por USB-C desde el XIAO. El ESP32-S3 regula internamente a 3.3 V.

**Consumo estimado en v1:**

| Componente | Consumo típico |
|---|---|
| XIAO ESP32-S3 (activo normal) | 40–80 mA |
| OLED 128×64 | 15–25 mA |
| Buzzer pasivo (activo) | 5–15 mA |
| Total estimado | < 100 mA |

Bien dentro del límite del regulador onboard del XIAO y de la corriente de un puerto USB estándar.

**Fase v2 (futura):** el XIAO ESP32-S3 básico incluye pads de batería LiPo y cargador integrado en la cara inferior. Para agregar batería solo se necesita soldar los pads + y − a una celda LiPo de 3.7 V. El cargador opera a 5 V desde el USB-C. Sin cambios en el firmware base.

---

## 6. Consideraciones para el cuerpo 3D

Al diseñar o modelar el cuerpo del espToy, tener en cuenta los siguientes puntos de integración:

**OLED:**
- Ventana rectangular de 28 × 28 mm aprox. (o ajustar al tamaño real del panel activo) en la cara frontal.
- El módulo OLED se apoya desde atrás con tolerancia de 0.2–0.3 mm. Puede fijarse con pegamento UV o encastre de fricción.

**Botones A y B:**
- Agujeros o canales en el cuerpo para que el émbolo de cada pulsador sea accesible desde el exterior.
- Separación mínima entre botones: 10–12 mm para que sean cómodos de presionar.

**Touch capacitivo:**
- Zona de pared delgada (1–2 mm) en el área de "caricia" (lomo o lateral del cuerpo).
- El parche conductor (cinta de cobre o aluminio) se pega por dentro de esa zona durante el ensamble.
- Evitar pintura metálica o relleno conductor en esa zona (interfiere con el sensado).

**Buzzer:**
- El buzzer es omnidireccional pero gana volumen con una pequeña cámara acústica detrás del diafragma.
- Opción 1: rejilla de orificios (∅ 1–1.5 mm) en la zona de salida de sonido.
- Opción 2: cavidad cerrada detrás del buzzer con un único orificio de salida (tipo Helmholtz simple) para reforzar la frecuencia principal.
- El buzzer no debe quedar en contacto directo con paredes sólidas sin salida de aire: pierde volumen.

**USB-C:**
- Acceso al conector USB-C del XIAO para carga y reprogramación sin desensamblar.
- Un slot en la base o lateral del cuerpo de aprox. 10 × 5 mm es suficiente.
- Considerar también acceso al botón RESET del XIAO si se necesita durante el desarrollo.
