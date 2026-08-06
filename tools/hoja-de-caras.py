#!/usr/bin/env python3
"""
Arma docs/img/caras.png a partir de los volcados de framebuffer que deja
tools/capturar-caras.ps1.

No es un dibujo aproximado hecho en la PC: cada imagen son los píxeles
exactos que tenía el OLED, bajados con el comando serial 'f'. Es la única
forma de revisar cómo quedó una cara sin sacarle una foto a la pantalla.

Uso:
    powershell -File tools/capturar-caras.ps1     # baja los 13 framebuffers
    python tools/hoja-de-caras.py <dir-capturas>  # arma la lámina
"""
import os
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 128, 64

# El orden en que las deja el script de captura: 'e' incrementa ANTES de
# mostrar, así que la primera es FELIZ y la última da la vuelta a NEUTRAL.
ORDEN = ["FELIZ", "TRISTE", "ENOJADO", "SORPRENDIDO", "ABURRIDO", "DORMIDO",
         "SOSPECHOSO", "AMOR", "GUINO", "RISA", "MAREADO", "ILUSIONADO",
         "NEUTRAL"]

# Las 13, con lo que dispara cada una. Desde que las cuatro huérfanas
# (triste, enojado, aburrido, amor) se engancharon a escalas de cosas que
# ya pasaban, no queda ninguna sin causa.
USADAS = [
    ("NEUTRAL",     "cara de reposo"),
    ("GUINO",       "cada 15-45 s; el ojo cierra y abre"),
    ("SOSPECHOSO",  "al azar 1-2.5 min, y a los 3 min sin uso"),
    ("ABURRIDO",    "a los 6 y 9 min sin uso"),
    ("DORMIDO",     "2.2 s antes de apagar la pantalla"),
    ("FELIZ",       "al volver de jugar (o RISA)"),
    ("RISA",        "al volver de jugar (o FELIZ)"),
    ("TRISTE",      "si entras al arcade y salis sin jugar"),
    ("ILUSIONADO",  "al alzarlo (IMU)"),
    ("AMOR",        "al sostenerlo en alto 2.5 s (IMU)"),
    ("SORPRENDIDO", "sacudida leve (IMU) + portal WiFi"),
    ("MAREADO",     "sacudirlo de mas (IMU)"),
    ("ENOJADO",     "si seguis sacudiendo tras el mareo (IMU)"),
]


def decodificar(hexs):
    """Framebuffer SSD13xx → imagen.

    Formato: 8 páginas de 128 bytes; cada byte son 8 píxeles VERTICALES,
    con el bit 0 arriba. Y se rota 180° porque el OLED va montado invertido
    y el firmware compensa con U8G2_R2: el buffer guarda el contenido sin
    rotar, así que hay que girarlo para ver lo mismo que ve el usuario.
    """
    b = bytes.fromhex(hexs)
    img = Image.new("L", (W, H), 0)
    px = img.load()
    for y in range(H):
        fila = (y // 8) * W
        bit = y % 8
        for x in range(W):
            if (b[fila + x] >> bit) & 1:
                px[x, y] = 255
    return img.rotate(180)


def fuente(sz):
    for n in ("segoeui.ttf", "arial.ttf", "DejaVuSans.ttf"):
        try:
            return ImageFont.truetype(n, sz)
        except Exception:
            pass
    return ImageFont.load_default()


def main():
    dircap = sys.argv[1] if len(sys.argv) > 1 else "caras"
    salida = sys.argv[2] if len(sys.argv) > 2 else "docs/img/caras.png"

    por_nombre = {}
    for i, nombre in enumerate(ORDEN):
        ruta = os.path.join(dircap, f"{i}.txt")
        hexs = open(ruta).read().split("\n")[1].strip()
        por_nombre[nombre] = decodificar(hexs)

    ESC = 3                     # cada píxel del OLED = 3x3 en la lámina
    CW, CH = W * ESC, H * ESC
    MARG, TIT, SUB, GAP = 16, 24, 18, 16
    COLS = 3
    FILAS = (len(USADAS) + COLS - 1) // COLS
    AW = MARG * 2 + COLS * CW + (COLS - 1) * GAP
    AH = MARG * 2 + 48 + FILAS * (CH + TIT + SUB) + (FILAS - 1) * GAP

    hoja = Image.new("RGB", (AW, AH), (24, 24, 28))
    d = ImageDraw.Draw(hoja)
    f_tit, f_lbl, f_sub = fuente(24), fuente(18), fuente(14)
    d.text((MARG, MARG), "Las 13 caras y que dispara cada una",
           font=f_tit, fill=(235, 235, 240))

    for i, (nombre, cuando) in enumerate(USADAS):
        c, r = i % COLS, i // COLS
        x = MARG + c * (CW + GAP)
        y = MARG + 48 + r * (CH + TIT + SUB + GAP)
        d.text((x, y), nombre, font=f_lbl, fill=(150, 200, 255))
        d.text((x, y + TIT - 4), cuando, font=f_sub, fill=(140, 140, 150))
        hoja.paste(por_nombre[nombre].resize((CW, CH), Image.NEAREST).convert("RGB"),
                   (x, y + TIT + SUB))
        d.rectangle([x - 1, y + TIT + SUB - 1, x + CW, y + TIT + SUB + CH],
                    outline=(70, 70, 80))

    os.makedirs(os.path.dirname(salida), exist_ok=True)
    hoja.save(salida)
    print(f"{salida} — {hoja.size[0]}x{hoja.size[1]}")


if __name__ == "__main__":
    main()
