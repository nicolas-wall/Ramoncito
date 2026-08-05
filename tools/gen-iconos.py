#!/usr/bin/env python3
"""
Genera src/iconos.h — los íconos del carrusel del arcade, en formato XBM.

Por qué un generador y no bytes escritos a mano: un icono de 32x32 son 128
bytes en hexadecimal, imposibles de revisar o corregir a ojo. Acá se dibujan
con primitivas, y el header sale con un preview en ASCII al lado de cada
icono para poder verificarlo leyendo el archivo.

Para regenerar:  python tools/gen-iconos.py
"""

W = H = 32


def lienzo():
    return [[0] * W for _ in range(H)]


def rect(b, x, y, w, h):
    """Rectángulo relleno."""
    for j in range(y, min(y + h, H)):
        for i in range(x, min(x + w, W)):
            if 0 <= i < W and 0 <= j < H:
                b[j][i] = 1


def marco(b, x, y, w, h, grosor=1):
    rect(b, x, y, w, grosor)
    rect(b, x, y + h - grosor, w, grosor)
    rect(b, x, y, grosor, h)
    rect(b, x + w - grosor, y, grosor, h)


def linea_punteada_v(b, x, y0, y1, on=3, off=3):
    """Línea vertical punteada — la red de la cancha."""
    j = y0
    while j < y1:
        rect(b, x, j, 1, min(on, y1 - j))
        j += on + off


def triangulo(b, x, y, alto, hacia="der"):
    """Triángulo relleno, usado para las flechas."""
    for k in range(alto):
        ancho = k + 1
        if hacia == "der":
            rect(b, x + k, y + alto - 1 - k, 1, 2 * k + 1)
        else:
            rect(b, x + alto - 1 - k, y + alto - 1 - k, 1, 2 * k + 1)


# ── Iconos ──────────────────────────────────────────────────────

def icono_pong():
    """Una cancha de Pong: marco, red punteada, dos paletas y la pelota."""
    b = lienzo()
    marco(b, 0, 2, 32, 28)
    linea_punteada_v(b, 16, 5, 27, on=3, off=3)
    rect(b, 3, 8, 2, 11)      # paleta izquierda
    rect(b, 27, 14, 2, 11)    # paleta derecha
    rect(b, 12, 17, 3, 3)     # pelota
    return b


def icono_snake():
    """Una víbora en zigzag con la comida delante."""
    b = lienzo()
    marco(b, 0, 2, 32, 28)
    # Cuerpo: segmentos de 3x3 con un hueco entre medio, en escalera
    for (cx, cy) in [(4, 20), (8, 20), (12, 20), (12, 16), (12, 12),
                     (16, 12), (20, 12), (20, 16)]:
        rect(b, cx, cy, 3, 3)
    rect(b, 20, 20, 4, 4)      # cabeza, un poco más grande
    marco(b, 5, 7, 4, 4)       # comida, hueca
    return b


def icono_breakout():
    """Filas de ladrillos, la paleta abajo y la pelota subiendo."""
    b = lienzo()
    for fila in range(3):
        for col in range(5):
            rect(b, 2 + col * 6, 4 + fila * 4, 5, 3)
    rect(b, 15, 19, 2, 2)      # pelota
    rect(b, 10, 26, 12, 2)     # paleta
    return b


def icono_invaders():
    """El bicho clásico: cuerpo, ojos, antenas y patas."""
    b = lienzo()
    rect(b, 8, 10, 16, 8)      # cuerpo
    rect(b, 6, 12, 2, 6)       # brazo izquierdo
    rect(b, 24, 12, 2, 6)      # brazo derecho
    rect(b, 10, 6, 2, 4)       # antena izquierda
    rect(b, 20, 6, 2, 4)       # antena derecha
    for x in (12, 18):         # ojos, en negativo
        for y in range(12, 15):
            for xx in range(x, x + 2):
                b[y][xx] = 0
    rect(b, 9, 18, 3, 4)       # patas
    rect(b, 20, 18, 3, 4)
    rect(b, 14, 24, 4, 2)      # base
    return b


def icono_tetris():
    """Una pieza L grande y sola. Un icono de 32x32 no da para dos piezas:
    a esta escala se leen como bloques sueltos y no como tetrominós."""
    b = lienzo()
    LADO = 9

    def celda(cx, cy):
        """Bloque con borde y hueco: así se ve dónde termina uno y empieza
        el otro, que es lo que hace reconocible a la pieza."""
        x, y = 6 + cx * LADO, 3 + cy * LADO
        rect(b, x, y, LADO, LADO)
        for j in range(y + 2, y + LADO - 2):
            for i in range(x + 2, x + LADO - 2):
                b[j][i] = 0

    celda(0, 0)
    celda(0, 1)
    celda(0, 2)
    celda(1, 2)
    return b


def icono_laberinto():
    """Un laberinto chico con la bolita adentro."""
    b = lienzo()
    marco(b, 2, 3, 28, 26)
    rect(b, 7, 3, 2, 12)
    rect(b, 14, 8, 2, 14)
    rect(b, 21, 3, 2, 12)
    rect(b, 7, 20, 9, 2)
    rect(b, 21, 20, 9, 2)
    rect(b, 24, 25, 4, 4)      # bolita
    return b


def icono_salir():
    """Una puerta con una flecha saliendo hacia la derecha."""
    b = lienzo()
    marco(b, 3, 3, 14, 26)        # marco de la puerta
    rect(b, 13, 15, 2, 2)         # picaporte
    rect(b, 19, 15, 9, 2)         # asta de la flecha
    triangulo(b, 26, 11, 5, "der")  # punta de la flecha
    return b


ICONOS = [
    ("ICONO_PONG",      icono_pong(),      "Pong"),
    ("ICONO_SNAKE",     icono_snake(),     "Snake"),
    ("ICONO_BREAKOUT",  icono_breakout(),  "Breakout"),
    ("ICONO_INVADERS",  icono_invaders(),  "Invaders"),
    ("ICONO_TETRIS",    icono_tetris(),    "Tetris"),
    ("ICONO_LABERINTO", icono_laberinto(), "Laberinto"),
    ("ICONO_SALIR",     icono_salir(),     "Salir"),
]


# ── Salida ──────────────────────────────────────────────────────

def a_xbm(b):
    """XBM: filas alineadas a byte, bit 0 (LSB) = pixel de más a la izquierda."""
    bytes_por_fila = (W + 7) // 8
    out = []
    for fila in b:
        for by in range(bytes_por_fila):
            v = 0
            for bit in range(8):
                x = by * 8 + bit
                if x < W and fila[x]:
                    v |= (1 << bit)
            out.append(v)
    return out


def preview(b):
    return ["// " + "".join("#" if p else "." for p in fila) for fila in b]


def main():
    partes = [
        "#pragma once",
        "#include <Arduino.h>",
        "",
        "// ============================================================",
        "//  iconos.h — GENERADO por tools/gen-iconos.py, no editar a mano.",
        "//",
        "//  Iconos del carrusel del arcade, 32x32, formato XBM para",
        "//  u8g2.drawXBM(). Cada uno lleva su preview en ASCII al lado:",
        "//  si algo se ve raro en la pantalla, se compara contra el dibujo",
        "//  sin tener que descifrar el hexadecimal.",
        "// ============================================================",
        "",
        f"static const uint8_t ICONO_W = {W};",
        f"static const uint8_t ICONO_H = {H};",
        "",
    ]

    for nombre, bmp, desc in ICONOS:
        datos = a_xbm(bmp)
        partes.append(f"// ── {desc} ──")
        partes.extend(preview(bmp))
        partes.append(f"static const uint8_t {nombre}[] PROGMEM = {{")
        for i in range(0, len(datos), 12):
            fila = ", ".join(f"0x{v:02X}" for v in datos[i:i + 12])
            partes.append(f"    {fila},")
        partes.append("};")
        partes.append("")

    with open("src/iconos.h", "w", encoding="utf-8") as f:
        f.write("\n".join(partes))
    print(f"src/iconos.h escrito — {len(ICONOS)} iconos de {W}x{H}")


if __name__ == "__main__":
    main()
