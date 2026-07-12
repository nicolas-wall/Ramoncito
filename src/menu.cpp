#include "menu.h"
#include <stdio.h>
#include <string.h>

// ──────────────────────────────────────────────────
// Constantes de layout (pantalla 128x64)
// ──────────────────────────────────────────────────
static const int SCREEN_W  = 128;
static const int SCREEN_H  = 64;

// Fila superior: "espToy vX.Y.Z" + hora
static const int HEADER_Y  = 10;   // baseline de la fuente chica (header izq)
static const int HORA_Y    = 12;   // baseline fuente grande (hora)
static const int SEP_Y     = 15;   // línea separadora horizontal

// Fila WiFi
static const int WIFI_Y    = 24;   // baseline

// Tres barras de stats
// Cada stat ocupa ~11 px de alto; primera a y=33 (baseline etiqueta)
static const int STAT_START_Y = 36; // baseline primera etiqueta
static const int STAT_STEP    = 12; // distancia entre filas

// Barra de progreso
static const int BAR_X      = 46;  // inicio barra
static const int BAR_W      = 72;  // ancho total del marco
static const int BAR_H      = 7;   // alto del marco
static const int NUM_X      = BAR_X + BAR_W + 2; // posición del número % (si entra)

// ──────────────────────────────────────────────────
// Helpers internos
// ──────────────────────────────────────────────────

// Dibuja barra de progreso (0-100) con etiqueta a la izquierda
// label: texto corto, val: 0-100, yBaseline: baseline de la etiqueta
static void dibujarBarra(U8G2 &u8, const char *label, uint8_t val, int yBaseline) {
    // Etiqueta (fuente ya seteada por el caller)
    u8.drawStr(0, yBaseline, label);

    // Marco de la barra: alineado verticalmente con la etiqueta
    int barY = yBaseline - BAR_H + 1; // tope de la caja
    u8.drawFrame(BAR_X, barY, BAR_W, BAR_H);

    // Relleno proporcional al valor (dejamos 1px de margen interno)
    if (val > 0) {
        int fillW = (int)((BAR_W - 2) * val / 100);
        if (fillW > BAR_W - 2) fillW = BAR_W - 2;
        if (fillW > 0) {
            u8.drawBox(BAR_X + 1, barY + 1, fillW, BAR_H - 2);
        }
    }

    // Número al lado derecho (si hay espacio — lo hay: 128-46-72-2 = 8 px, caben 2 dígitos en 5x8)
    char numBuf[5];
    snprintf(numBuf, sizeof(numBuf), "%d", (int)val);
    u8.drawStr(NUM_X, yBaseline, numBuf);
}

// Trunca src en dst con ">" si no entra en maxChars caracteres (sin incluir terminador)
static void truncarSSID(char *dst, const char *src, int maxChars) {
    if ((int)strlen(src) <= maxChars) {
        strncpy(dst, src, maxChars);
        dst[maxChars] = '\0';
    } else {
        strncpy(dst, src, maxChars - 1);
        dst[maxChars - 1] = '>';  // indicador de truncado
        dst[maxChars]     = '\0';
    }
}

// ──────────────────────────────────────────────────
// Función principal de render
// ──────────────────────────────────────────────────
void menuRender(U8G2 &u8, const MenuData &d) {

    // ── FILA SUPERIOR IZQUIERDA: "espToy" + version ──
    u8.setFont(u8g2_font_5x8_tf);  // fuente chica 5x8

    char headerBuf[32];
    snprintf(headerBuf, sizeof(headerBuf), "espToy %s", d.fwVersion ? d.fwVersion : "");
    u8.drawStr(0, HEADER_Y, headerBuf);

    // ── FILA SUPERIOR DERECHA: HORA ──
    // Fuente mediana/grande para la hora
    u8.setFont(u8g2_font_9x15B_tf);  // ~9px ancho, 15px alto
    char horaBuf[6];
    if (d.horaValida) {
        snprintf(horaBuf, sizeof(horaBuf), "%02d:%02d", d.hora, d.minuto);
    } else {
        snprintf(horaBuf, sizeof(horaBuf), "--:--");
    }
    // Alinear a la derecha: cada carácter ocupa ~9px, string de 5 chars = ~45px
    int horaW = (int)u8.getStrWidth(horaBuf);
    u8.drawStr(SCREEN_W - horaW, HORA_Y, horaBuf);

    // ── LÍNEA SEPARADORA ──
    u8.drawHLine(0, SEP_Y, SCREEN_W);

    // ── FILA WIFI ──
    u8.setFont(u8g2_font_5x8_tf);

    char wifiBuf[32];
    if (d.portalActivo) {
        // Portal activo: mostrar nombre de la red del portal
        snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: portal setup");
    } else if (d.wifiConfigurada && d.horaValida) {
        // WiFi conectada y hora sincronizada
        // Espacio disponible para SSID: "WiFi: " (6) + SSID + " (sync OK)" (9) = 15 chars + SSID
        // Pantalla 128px / 5px por char = 25 chars totales; "WiFi: " = 6, " OK" = 3 => SSID max=16
        char ssidTrunc[17];
        truncarSSID(ssidTrunc, d.ssid ? d.ssid : "", 12);
        snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %s OK", ssidTrunc);
    } else if (d.wifiConfigurada && !d.horaValida) {
        // WiFi configurada pero sin sync de hora
        char ssidTrunc[17];
        truncarSSID(ssidTrunc, d.ssid ? d.ssid : "", 11);
        snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: %s s/sync", ssidTrunc);
    } else {
        // Sin configuracion
        snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: no configurado");
    }
    u8.drawStr(0, WIFI_Y, wifiBuf);

    // ── TRES BARRAS DE STATS ──
    u8.setFont(u8g2_font_5x8_tf);

    dibujarBarra(u8, "Feliz",    d.felicidad,   STAT_START_Y);
    dibujarBarra(u8, "Energia",  d.energia,     STAT_START_Y + STAT_STEP);
    dibujarBarra(u8, "Aburrim.", d.aburrimiento, STAT_START_Y + STAT_STEP * 2);
}
