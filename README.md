# Ramoncito

Mini arcade de escritorio sobre **Seeed Studio XIAO ESP32-S3**, con pantalla
OLED SSD1309 128×64, palanca analógica y tres botones. Cuando nadie juega,
muestra una carita que reacciona cada tanto y responde a que lo alcen o lo
sacudan.

## Estado actual — FW 0.11.0

Andando: menú de juegos, **Pong** contra la CPU, menú de sistema (hora, red,
firmware, ajustes), animación de encendido tipo CRT, y actualización OTA
automática desde GitHub Releases.

Pendiente: la palanca física (KY-023, a comprar). Mientras tanto el firmware
la emula por serial, así que todo es jugable y testeable sin ella — ver
[Comandos seriales](#comandos-seriales).

### De mascota a arcade

El proyecto arrancó como mascota tipo Tamagotchi: humor, personalidad que
evolucionaba con el uso, sensores táctiles de caricia y cosquillas,
mensajería por Telegram y un dashboard web en la LAN.

Con el pivot a arcade (0.10.0 y 0.11.0) todo eso se retiró. El motivo de
fondo, más allá de la decisión de producto: los sensores táctiles eran lo
único que alimentaba el humor, y sus pines pasaron a la palanca. Un sistema
de humor con el decaimiento intacto y sin ninguna entrada solo puede derivar
hacia la cara triste, sin manera de revertirlo. Mantenerlo habría sido
mostrar un medidor que nadie puede mover.

Lo que sobrevive de esa etapa es lo que no dependía de esas entradas: la
carita con sus gestos ocasionales y las reacciones al movimiento del IMU.

## Controles

| Control | Pin | Qué hace |
|---|---|---|
| Botón A | D0 | Menú de sistema / avanzar de página / **atrás** en el arcade |
| Botón B | D8 | Mover el cursor |
| Botón C | D2 | Activar; desde la cara, **entra al arcade** |
| Palanca | D1 / D9 / D10 | Ejes X e Y + pulsador |

Dentro del arcade, **A es siempre "atrás"**: del menú sale a la cara, del
juego pasa a pausa, de la pausa vuelve al menú.

Pinout completo y notas de cableado: [`docs/01-HARDWARE.md`](docs/01-HARDWARE.md)

## Comandos básicos

```bash
pio run                          # compilar
pio run -t upload                # compilar y flashear
pio device monitor               # monitor serial (115200 baud)
pio run -t upload -t monitor     # todo junto
```

## Comandos seriales

```
h N       fuerza la hora; h -1 libera        u    fuerza chequeo de OTA
s         alterna el sonido                  n    alterna el menú
p         fuerza el portal WiFi              i    imprime estado
```

Sin la palanca conectada, estos emulan los controles:

```
1 / 2 / 3   botones A / B / C        z   pulsador de la palanca
w x a d     palanca ↑ ↓ ← →          (es w-x-a-d porque 's' ya es el sonido)
```

Diagnóstico de cableado, útil al montar la palanca:

```
k    escáner de pines: imprime el nivel de todos los pines libres
v    vigilancia on/off: imprime cada flanco, muestreando a la velocidad
     del loop (no se pierde una pulsación corta)
```

Con `v` activo, mantener un botón apretado dice exactamente en qué GPIO está
cableado. Un pin clavado en `1` es circuito abierto (GND flojo o soldadura
abierta); uno clavado en `0`, las dos patas del par que el tact switch trae
cortocircuitado de fábrica.

## Credenciales WiFi

Normalmente se configuran desde el portal cautivo: el aparato levanta el AP
**Ramoncito-setup** y ahí se elige la red. `include/secrets.h` es opcional;
copiá `include/secrets.h.example` si lo querés precargado. Está en
`.gitignore` y nunca se sube.

El WiFi sirve para la hora por NTP y para el OTA automático. Sobre la LAN
queda un único endpoint vivo, `http://ramoncito.local/update`, para flashear
desde el navegador sin cable.

## Documentación

| Documento | Contenido |
|---|---|
| [`docs/00-PLAN-MAESTRO.md`](docs/00-PLAN-MAESTRO.md) | Plan general del proyecto |
| [`docs/01-HARDWARE.md`](docs/01-HARDWARE.md) | Pinout, BOM, cableado, diagnóstico |
| [`docs/03-EXPRESIONES.md`](docs/03-EXPRESIONES.md) | Motor de caras y animaciones |
| [`docs/04-PONG.md`](docs/04-PONG.md) | Diseño de Pong |
