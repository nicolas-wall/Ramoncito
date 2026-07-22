# Ramoncito

Mascota virtual tipo Tamagotchi construida sobre el **Seeed Studio XIAO ESP32-S3**,
con pantalla OLED SSD1309 128×64, y un juego de Pong oculto.

## Estado actual

**Etapa 0 completada** — Hola Mundo: arranque serial, parpadeo de LED y
reporte de heap libre cada segundo. Verificación de entorno lista.

Para el plan completo del proyecto ver: [`docs/00-PLAN-MAESTRO.md`](docs/00-PLAN-MAESTRO.md)

## Hardware objetivo

| Componente | Modelo |
|---|---|
| Microcontrolador | Seeed Studio XIAO ESP32-S3 |
| Pantalla | OLED SSD1309 128×64 vía I2C/SPI |
| LED indicador | GPIO21 (activo en BAJO, incorporado) |

## Comandos básicos

```bash
# Compilar el firmware
pio run

# Compilar y flashear
pio run -t upload

# Abrir monitor serial (115200 baud)
pio device monitor

# Compilar + flashear + monitorear de una vez
pio run -t upload && pio device monitor
```

## Credenciales WiFi

Copiá `include/secrets.h.example` a `include/secrets.h` y completá tus datos.
El archivo `secrets.h` está en `.gitignore` y nunca se sube al repositorio.
