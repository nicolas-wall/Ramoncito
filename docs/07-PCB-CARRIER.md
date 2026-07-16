# espToy — Placa carrier (Camino A)

Placa que sostiene el **XIAO ESP32-S3** + los módulos que ya se usan y probaron.
El firmware NO cambia: mismos pines. La placa solo reemplaza el cableado.

## Componentes sobre la placa
- XIAO ESP32-S3 (headers hembra 2×7, paso 2.54 mm, separación 0.6")
- Módulo OLED SSD1309 128×64 I2C (4 pines)
- Módulo MPU6050 / GY-521 (se usan 5 de sus 8 pines)
- Buzzer pasivo (módulo 3 pines, o buzzer bare 2 pads)
- Botón táctil (THT o SMD)
- Conector JST-PH 2 pines para la batería LiPo (BAT+ / BAT−)
- Interruptor deslizante (SPST) en la línea BAT+
- Conector 2–3 pines para los cables de los electrodos táctiles (cabeza/pie)

> Alimentación: el XIAO ESP32-S3 **carga la LiPo por sí mismo** (pads BAT+/BAT−
> en su cara inferior, carga vía su USB-C). No hace falta TP4056 aparte.

## Netlist (todas las conexiones)

| Red | Nodos conectados |
|-----|------------------|
| **3V3** | XIAO 3V3 → OLED VCC, MPU VCC, MPU **AD0**, Buzzer VCC |
| **GND** | XIAO GND → OLED GND, MPU GND, Buzzer GND, Botón pata2, JST batería (−) |
| **I2C_SDA** (GPIO5 / D4) | XIAO D4 → OLED SDA, MPU SDA |
| **I2C_SCL** (GPIO6 / D5) | XIAO D5 → OLED SCL, MPU SCL |
| **BUZZER** (GPIO4 / D3) | XIAO D3 → Buzzer S |
| **BTN** (GPIO1 / D0) | XIAO D0 → Botón pata1 (pata2 a GND) |
| **TOUCH_CABEZA** (GPIO3 / D2) | XIAO D2 → conector táctil pin 1 (cable al electrodo de la cabeza) |
| **TOUCH_PIE** (GPIO7 / D8) | XIAO D8 → conector táctil pin 2 (cable al electrodo del pie) |
| **BAT+** | JST batería (+) → Interruptor → XIAO BAT+ (pad inferior) |
| **BAT−** | JST batería (−) → XIAO BAT− (pad inferior) |

Pines del MPU6050 que quedan **sin conectar**: XDA, XCL, INT.
El **AD0 va a 3V3** → dirección I2C 0x69 (evita choque con futuro RTC en 0x68).

## Notas de diseño (importantes)
- **Pull-ups I2C**: los módulos OLED y MPU ya traen sus resistencias de pull-up en
  SDA/SCL. Con módulos, **NO agregar** pull-ups extra en la placa (quedarían en
  paralelo, demasiado fuertes). Si algún día usás los chips pelados (Camino B),
  ahí sí poné 4.7 kΩ de SDA y SCL a 3V3.
- **Interruptor**: va en serie en BAT+ (entre el JST de la batería y el XIAO).
  Apaga el juguete cortando la batería. Con USB conectado el XIAO igual anda y carga.
- **Bus I2C compartido**: SDA (D4) y SCL (D5) son una sola pista cada una que toca
  la pantalla y el MPU en paralelo.
- **Táctiles**: cada electrodo (cabeza/pie) es un cable que llega a un pin del
  conector; el electrodo en sí (una superficie de cobre/lámina) va en el cuerpo 3D.
- **Batería (aún sin comprar)**: dejar solo el conector JST-PH 2 pines + un hueco/área
  para pegar la celda. El diseño NO depende del modelo exacto; elegí después una LiPo
  de 3.7 V ~400–600 mAh que entre en el cuerpo. Corriente de carga del XIAO ~100 mA
  (default), apta para esas capacidades.

## Layout / mecánica
- Pantalla al frente, con su área visible alineada a la ventana de la carcasa.
- USB-C del XIAO hacia un borde accesible (para cargar/flashear).
- Botón accesible desde afuera.
- JST de batería y el interruptor cerca del borde, del lado del hueco de la batería.
- Placa 2 capas alcanza y sobra. Tamaño lo define la carcasa 3D.

## Pedido (JLCPCB)
- Placa pelada: mínimo **5 unidades**. Subís Gerbers desde EasyEDA/KiCad.
- Con headers, el ensamblado lo hacés vos a mano (soldás XIAO + módulos + JST + switch).
