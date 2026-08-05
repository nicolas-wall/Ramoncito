#include "menu.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

// ──────────────────────────────────────────────────
// Layout (pantalla 128x64)
// ──────────────────────────────────────────────────
static const int SCREEN_W = 128;

// Encabezado: título a la izquierda, reloj grande a la derecha.
static const int TITULO_Y = 8;    // baseline del título (fuente 5x8)
static const int SEP_Y    = 12;   // línea separadora

// Nombres cortos de día de semana, índice 0=domingo (como tm_wday). Sin acentos.
static const char* const DIAS_SEMANA[7] = {
    "dom", "lun", "mar", "mie", "jue", "vie", "sab"
};

// Etiquetas de las opciones de Ajustes, en el orden de ajustesSel.
static const char* const AJUSTES_LABEL[MENU_AJUSTES_OPTS] = {
    "Sonido", "Cambiar WiFi"
};

// ──────────────────────────────────────────────────
// Encabezado común a las dos páginas
// ──────────────────────────────────────────────────
static void dibujarHeader(U8G2 &u8, const MenuData &d,
                          const char* titulo, uint8_t pagina) {
    u8.setFont(u8g2_font_5x8_tf);
    u8.drawStr(0, TITULO_Y, titulo);

    // Indicador de página, alineado a la derecha: "1/2"
    char pag[8];
    snprintf(pag, sizeof(pag), "%u/%u", pagina, MENU_PAGINAS);
    u8.drawStr(SCREEN_W - u8.getStrWidth(pag), TITULO_Y, pag);

    u8.drawHLine(0, SEP_Y, SCREEN_W);
}

// ──────────────────────────────────────────────────
// Página 1 — hora, red, firmware
// ──────────────────────────────────────────────────
static void renderInfo(U8G2 &u8, const MenuData &d) {
    dibujarHeader(u8, d, "SISTEMA", 1);

    char buf[32];

    // Reloj grande centrado: es el dato que más se mira de reojo.
    u8.setFont(u8g2_font_logisoso16_tn);
    if (d.horaValida) snprintf(buf, sizeof(buf), "%02d:%02d", d.hora, d.minuto);
    else              snprintf(buf, sizeof(buf), "--:--");
    u8.drawStr((SCREEN_W - u8.getStrWidth(buf)) / 2, 31, buf);

    u8.setFont(u8g2_font_4x6_tf);
    if (d.horaValida) {
        snprintf(buf, sizeof(buf), "%s %02d/%02d",
                 DIAS_SEMANA[d.diaSemana % 7], d.dia, d.mes);
        u8.drawStr((SCREEN_W - u8.getStrWidth(buf)) / 2, 39, buf);
    }

    // Estado de red
    u8.setFont(u8g2_font_5x7_tf);
    if (d.portalActivo) {
        snprintf(buf, sizeof(buf), "portal: %s", PORTAL_AP_SSID);
    } else if (d.staConectada && d.lanIP[0] != '\0') {
        snprintf(buf, sizeof(buf), "%s", d.lanIP);
    } else if (d.wifiConfigurada) {
        snprintf(buf, sizeof(buf), "%s (sin conexion)", d.ssid);
    } else {
        snprintf(buf, sizeof(buf), "sin WiFi configurada");
    }
    u8.drawStr(0, 51, buf);

    // Firmware, y aviso de update si lo hay
    if (d.hayUpdate) snprintf(buf, sizeof(buf), "v%s  ->  v%s !", d.fwVersion, d.versionNueva);
    else             snprintf(buf, sizeof(buf), "v%s", d.fwVersion);
    u8.drawStr(0, 61, buf);
}

// ──────────────────────────────────────────────────
// Página 2 — ajustes
// ──────────────────────────────────────────────────
static void renderAjustes(U8G2 &u8, const MenuData &d) {
    dibujarHeader(u8, d, "AJUSTES", 2);

    u8.setFont(u8g2_font_6x12_tf);
    for (uint8_t i = 0; i < MENU_AJUSTES_OPTS; i++) {
        int y = 28 + i * 16;
        bool sel = (d.ajustesSel == i);

        if (sel) {
            // Resaltado en negativo, igual que el menú del arcade: una sola
            // convención visual para "esto es lo que vas a activar".
            u8.drawBox(0, y - 10, SCREEN_W, 13);
            u8.setDrawColor(0);
        }

        u8.drawStr(4, y, AJUSTES_LABEL[i]);

        // El estado del sonido va a la derecha, en la misma fila
        if (i == 0) {
            const char* estado = d.sonidoHabilitado ? "ON" : "OFF";
            u8.drawStr(SCREEN_W - 4 - u8.getStrWidth(estado), y, estado);
        }

        if (sel) u8.setDrawColor(1);
    }

    u8.setFont(u8g2_font_4x6_tf);
    const char* ayuda = "B mueve   C activa   A pagina";
    u8.drawStr((SCREEN_W - u8.getStrWidth(ayuda)) / 2, 62, ayuda);
}

// ──────────────────────────────────────────────────
void menuRender(U8G2 &u8, const MenuData &d, uint8_t pagina) {
    if (pagina <= 1) renderInfo(u8, d);
    else             renderAjustes(u8, d);
}
