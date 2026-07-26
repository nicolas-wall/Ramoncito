// =============================================================
//  Ramoncito — notify.cpp
// =============================================================
#include "notify.h"
#include <string.h>

Notify notify;

// ----- Cola ---------------------------------------------------
void Notify::push(const char* titulo, const char* texto, NotifIcon icono) {
    uint8_t idx;
    if (_count < CAP) {
        idx = (_head + _count) % CAP;
        _count++;
    } else {
        // Cola llena: pisar el más viejo y avanzar la cabeza
        _head = (_head + 1) % CAP;
        idx = (_head + _count - 1) % CAP;
    }
    Notif &n = _q[idx];
    snprintf(n.titulo, sizeof(n.titulo), "%s", titulo ? titulo : "Aviso");
    snprintf(n.texto,  sizeof(n.texto),  "%s", texto  ? texto  : "");
    n.icono = icono;
}

bool Notify::pop(Notif& out) {
    if (_count == 0) return false;
    out = _q[_head];
    _head = (_head + 1) % CAP;
    _count--;
    return true;
}

NotifIcon Notify::iconoDeTexto(const char* s) {
    if (!s) return NotifIcon::BELL;
    if (!strcmp(s, "chat"))   return NotifIcon::CHAT;
    if (!strcmp(s, "mail"))   return NotifIcon::MAIL;
    if (!strcmp(s, "alerta")) return NotifIcon::ALERTA;
    if (!strcmp(s, "reloj"))  return NotifIcon::RELOJ;
    return NotifIcon::BELL;
}

// ----- Íconos 16x16 (dibujados con primitivas) ----------------
void Notify::_drawIcono(U8G2 &u8, NotifIcon ic, int x, int y) {
    switch (ic) {
        case NotifIcon::CHAT:  // globo de chat
            u8.drawRFrame(x, y, 16, 12, 3);
            u8.drawTriangle(x+3, y+11, x+9, y+11, x+3, y+15);
            u8.drawDisc(x+5,  y+6, 1);
            u8.drawDisc(x+8,  y+6, 1);
            u8.drawDisc(x+11, y+6, 1);
            break;
        case NotifIcon::MAIL:  // sobre
            u8.drawFrame(x, y+1, 16, 12);
            u8.drawLine(x, y+1, x+8, y+7);
            u8.drawLine(x+15, y+1, x+8, y+7);
            break;
        case NotifIcon::BELL:  // campana
            u8.drawDisc(x+8, y+1, 1);
            u8.drawTriangle(x+2, y+12, x+14, y+12, x+8, y+2);
            u8.drawBox(x+2, y+12, 13, 1);
            u8.drawDisc(x+8, y+14, 1);
            break;
        case NotifIcon::ALERTA:  // triángulo con !
            u8.drawTriangle(x+8, y, x+1, y+14, x+15, y+14);
            u8.drawVLine(x+8, y+5, 5);
            u8.drawPixel(x+8, y+12);
            break;
        case NotifIcon::RELOJ:  // reloj
            u8.drawCircle(x+8, y+7, 7);
            u8.drawLine(x+8, y+7, x+8, y+3);
            u8.drawLine(x+8, y+7, x+11, y+9);
            break;
    }
}

// ----- Render: globo de diálogo centrado ----------------------
// Dibuja un bocadillo de cómic (marco redondeado + colita hacia abajo)
// con el texto centrado adentro. Sin título "Mensaje": parece que el toy
// lo está diciendo. Si el aviso trae título (webhook), lo muestra chico.
void Notify::render(U8G2 &u8, const Notif &n, uint32_t ahora, uint32_t mostradaDesde) {
    // ── Globo ──
    const int bx = 4, by = 2, bw = 120, bh = 50;   // marco (bottom en y=51)
    u8.drawRFrame(bx, by, bw, bh, 9);
    // Colita: triángulo abajo-izquierda, apuntando hacia la "boca"
    const int tx = 30, ty = by + bh + 8;           // punta de la cola
    u8.drawLine(23, by + bh - 1, tx, ty);
    u8.drawLine(39, by + bh - 1, tx, ty);
    // "Abrir" el borde inferior del globo entre la base de la cola
    u8.setDrawColor(0);
    u8.drawHLine(24, by + bh - 1, 15);
    u8.setDrawColor(1);

    // ── Texto: word-wrap centrado ──
    u8.setFont(u8g2_font_5x8_tf);
    const int GLYPH = 5;                 // ancho de la fuente 5x8
    const int MAXCPL = 20;               // ~104 px útiles / 5
    const int MAXLIN = 10;
    char lineas[MAXLIN][MAXCPL + 1];
    int nlin = 0;
    {
        const char* p = n.texto;
        while (*p && nlin < MAXLIN) {
            while (*p == ' ') p++;
            if (!*p) break;
            int len = 0, corte = 0;
            const char* q = p;
            while (q[len] && q[len] != '\n' && len < MAXCPL) {
                if (q[len] == ' ') corte = len;
                len++;
            }
            int usar;
            if (q[len] == '\0' || q[len] == '\n') usar = len;
            else if (corte > 0)                    usar = corte;
            else                                   usar = MAXCPL;
            if (usar > MAXCPL) usar = MAXCPL;
            memcpy(lineas[nlin], q, usar);
            lineas[nlin][usar] = '\0';
            nlin++;
            p = q + usar;
            if (*p == '\n') p++;
        }
    }

    // Título opcional (webhook): chico y centrado arriba del texto
    bool hayTitulo = (n.titulo[0] != '\0');
    int areaTop = by + (hayTitulo ? 15 : 5);
    int areaBot = by + bh - 4;
    if (hayTitulo) {
        int tw = (int)strlen(n.titulo) * 6;
        u8.setFont(u8g2_font_6x10_tf);
        u8.drawStr((128 - tw) / 2, by + 12, n.titulo);
        u8.setFont(u8g2_font_5x8_tf);
    }

    // Área visible y scroll vertical si hay muchas líneas
    const int lineH = 9;
    int visibles = (areaBot - areaTop) / lineH;
    if (visibles < 1) visibles = 1;
    int offset = 0;
    if (nlin > visibles) {
        int32_t t = (int32_t)(ahora - mostradaDesde) - 2000;
        if (t < 0) t = 0;
        offset = t / 1200;
        int maxoff = nlin - visibles;
        if (offset > maxoff) offset = maxoff;
    }
    int shown = nlin - offset; if (shown > visibles) shown = visibles;
    int blockH = shown * lineH;
    int startY = areaTop + ((areaBot - areaTop) - blockH) / 2;
    for (int i = 0; i < visibles; i++) {
        int li = i + offset;
        if (li >= nlin) break;
        int w = (int)strlen(lineas[li]) * GLYPH;
        int x = (128 - w) / 2; if (x < bx + 3) x = bx + 3;
        u8.drawStr(x, startY + i * lineH + 7, lineas[li]);
    }
}
