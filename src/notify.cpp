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

// ----- Render de la tarjeta -----------------------------------
void Notify::render(U8G2 &u8, const Notif &n, uint32_t ahora, uint32_t mostradaDesde) {
    // Cabecera: ícono + título
    _drawIcono(u8, n.icono, 3, 3);
    u8.setFont(u8g2_font_6x12_tf);
    // Título recortado a lo que entra a la derecha del ícono (x=24..127)
    char tit[18];
    snprintf(tit, sizeof(tit), "%s", n.titulo);
    u8.drawStr(24, 14, tit);
    u8.drawHLine(0, 20, 128);

    // Cuerpo: word-wrap del texto en líneas de ~24 chars (fuente 5x8)
    u8.setFont(u8g2_font_5x8_tf);
    const int MAXCPL = 24;      // chars por línea
    const int MAXLIN = 12;      // tope de líneas
    char lineas[MAXLIN][MAXCPL + 1];
    int nlin = 0;
    {
        const char* p = n.texto;
        while (*p && nlin < MAXLIN) {
            // Saltar espacios iniciales
            while (*p == ' ') p++;
            if (!*p) break;
            int len = 0, corte = 0;
            const char* q = p;
            // Avanzar hasta MAXCPL, recordando el último espacio para cortar por palabra
            while (q[len] && q[len] != '\n' && len < MAXCPL) {
                if (q[len] == ' ') corte = len;
                len++;
            }
            int usar;
            if (q[len] == '\0' || q[len] == '\n') usar = len;      // entra entero
            else if (corte > 0)                    usar = corte;    // cortar por palabra
            else                                   usar = MAXCPL;   // palabra larguísima: cortar duro
            if (usar > MAXCPL) usar = MAXCPL;
            memcpy(lineas[nlin], q, usar);
            lineas[nlin][usar] = '\0';
            nlin++;
            p = q + usar;
            if (*p == '\n') p++;
        }
    }

    // Área visible: y=30..62, lineH=9 → 4 líneas. Si hay más, scroll vertical.
    const int lineH = 9, visibles = 4;
    int offset = 0;
    if (nlin > visibles) {
        // Empieza a scrollear a los 2 s, avanza 1 línea cada 1.2 s, se detiene al final
        int32_t t = (int32_t)(ahora - mostradaDesde) - 2000;
        if (t < 0) t = 0;
        offset = t / 1200;
        int maxoff = nlin - visibles;
        if (offset > maxoff) offset = maxoff;
    }
    for (int i = 0; i < visibles; i++) {
        int li = i + offset;
        if (li >= nlin) break;
        u8.drawStr(2, 30 + i * lineH + 7, lineas[li]);
    }
}
