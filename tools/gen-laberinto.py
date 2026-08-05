#!/usr/bin/env python3
"""
Genera el mapa del juego de laberinto e imprime el bloque C++ listo para
pegar en src/laberinto.cpp.

Por qué generado y no dibujado a mano: el primer laberinto de este juego se
dibujó a mano y resultó tener la meta AISLADA — solo 58 de 221 celdas eran
alcanzables desde la entrada. Un laberinto sin solución no se nota leyendo
el código ni compilando: se descubre jugando, después de perder un minuto.
Acá el mapa se verifica por inundación antes de imprimirse.

Uso:  python tools/gen-laberinto.py [semilla]
"""
import random
import sys
from collections import deque

CELDAS_X, CELDAS_Y = 12, 5          # celdas lógicas del laberinto
W, H = 2 * CELDAS_X + 1, 2 * CELDAS_Y + 1   # 25 x 11 caracteres
ATAJOS = 6                          # paredes extra que se abren (ver abajo)


def generar(semilla):
    random.seed(semilla)
    g = [['#'] * W for _ in range(H)]

    # Backtracking recursivo: garantiza que todas las celdas queden
    # conectadas, porque solo excava hacia celdas no visitadas.
    vis = [[False] * CELDAS_X for _ in range(CELDAS_Y)]
    g[1][1] = '.'
    vis[0][0] = True
    pila = [(0, 0)]
    while pila:
        cx, cy = pila[-1]
        opciones = []
        for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            nx, ny = cx + dx, cy + dy
            if 0 <= nx < CELDAS_X and 0 <= ny < CELDAS_Y and not vis[ny][nx]:
                opciones.append((nx, ny, dx, dy))
        if not opciones:
            pila.pop()
            continue
        nx, ny, dx, dy = random.choice(opciones)
        g[2 * cy + 1 + dy][2 * cx + 1 + dx] = '.'   # abrir la pared del medio
        g[2 * ny + 1][2 * nx + 1] = '.'
        vis[ny][nx] = True
        pila.append((nx, ny))

    # Un laberinto perfecto tiene un único camino entre dos puntos, y eso
    # se vuelve tedioso: equivocarse obliga a desandar todo. Abrir unas
    # pocas paredes crea ciclos y deja improvisar atajos.
    abiertos = 0
    intentos = 0
    while abiertos < ATAJOS and intentos < 500:
        intentos += 1
        x, y = random.randrange(1, W - 1), random.randrange(1, H - 1)
        if g[y][x] != '#':
            continue
        horiz = g[y][x - 1] == '.' and g[y][x + 1] == '.'
        vert = g[y - 1][x] == '.' and g[y + 1][x] == '.'
        if horiz or vert:
            g[y][x] = '.'
            abiertos += 1

    g[2 * (CELDAS_Y - 1) + 1][2 * (CELDAS_X - 1) + 1] = 'S'   # meta
    return g


def verificar(g):
    """Inundación desde la entrada: la meta tiene que ser alcanzable y no
    puede quedar ninguna celda suelta."""
    vis = {(1, 1)}
    q = deque([(1, 1)])
    while q:
        y, x = q.popleft()
        for dy, dx in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            ny, nx = y + dy, x + dx
            if 0 <= ny < H and 0 <= nx < W and (ny, nx) not in vis and g[ny][nx] != '#':
                vis.add((ny, nx))
                q.append((ny, nx))

    meta = next((y, x) for y, f in enumerate(g) for x, c in enumerate(f) if c == 'S')
    libres = sum(1 for f in g for c in f if c != '#')
    if meta not in vis:
        raise SystemExit("FALLA: la meta quedó aislada de la entrada")
    if len(vis) != libres:
        raise SystemExit(f"FALLA: {libres - len(vis)} celdas sueltas")
    return libres


def main():
    semilla = int(sys.argv[1]) if len(sys.argv) > 1 else 20260805
    g = generar(semilla)
    libres = verificar(g)
    print(f"// {W}x{H}, semilla {semilla}, {libres} celdas libres, "
          f"meta verificada alcanzable")
    print("static const char* const MAPA[LAB_FILAS] = {")
    for fila in g:
        print('    "%s",' % "".join(fila))
    print("};")


if __name__ == "__main__":
    main()
