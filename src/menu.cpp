#include "menu.h"
#include "config.h"
#include <stdio.h>
#include <string.h>

// ──────────────────────────────────────────────────
// Constantes de layout (pantalla 128x64)
// ──────────────────────────────────────────────────
static const int SCREEN_W  = 128;
static const int SCREEN_H  = 64;

// Header (y 0-14): izquierda "espToy" + fecha, derecha reloj HH:MM
// Los ~48 px de la derecha (x=80..127) son EXCLUSIVOS del reloj.
static const int RELOJ_ZONA_X = 80;  // el texto de la izquierda nunca pasa de acá
static const int TITULO_Y     = 5;   // baseline "espToy" (fuente 4x6, línea 1)
static const int FECHA_Y      = 14;  // baseline fecha (fuente 5x8, línea 2)
static const int HORA_Y       = 12;  // baseline reloj (fuente 9x15B)
static const int SEP_Y        = 15;  // línea separadora horizontal

// Fila WiFi
static const int WIFI_Y    = 24;   // baseline

// Tres barras de stats
static const int STAT_START_Y = 36; // baseline primera etiqueta
static const int STAT_STEP    = 12; // distancia entre filas

// Columnas de cada fila de stat:
//   etiqueta: x=0..44 | barra: x=46..99 | numero: alineado a derecha en x=113..127
static const int BAR_X      = 46;  // inicio barra
static const int BAR_W      = 54;  // ancho total del marco (46+54 = 100)
static const int BAR_H      = 7;   // alto del marco
// Número con "%3u" en 5x8 (monoespaciada): 3 chars * 5 px = 15 px.
// Alineado a la derecha: 128 - 15 = 113 → ocupa x=113..127, "100" entra SIEMPRE.
static const int NUM_W      = 15;
static const int NUM_X      = SCREEN_W - NUM_W;

// Nombres cortos de día de semana, índice 0=domingo (como tm_wday). Sin acentos.
static const char* const DIAS_SEMANA[7] = {
    "dom", "lun", "mar", "mie", "jue", "vie", "sab"
};

// ──────────────────────────────────────────────────
// Helpers internos
// ──────────────────────────────────────────────────

// Elige la palabra descriptiva de un rasgo según su valor (0-100)
// bajo: valor < PERSONALIDAD_UMBRAL_BAJO (35)
// medio: 35 <= valor < PERSONALIDAD_UMBRAL_ALTO (65)
// alto: valor >= 65
// Las opciones son: opBajo, opMedio, opAlto
static const char* elegirPalabraRasgo(uint8_t valor,
                                      const char* opBajo, const char* opMedio, const char* opAlto) {
    if (valor < PERSONALIDAD_UMBRAL_BAJO) return opBajo;
    if (valor >= PERSONALIDAD_UMBRAL_ALTO)  return opAlto;
    return opMedio;
}

// Dibuja barra de progreso (0-100) con etiqueta a la izquierda y número a la derecha
// label: texto corto, val: 0-100, yBaseline: baseline de la etiqueta
static void dibujarBarra(U8G2 &u8, const char *label, uint8_t val, int yBaseline) {
    // Etiqueta (fuente 5x8 ya seteada por el caller)
    u8.drawStr(0, yBaseline, label);

    // Marco de la barra: alineado verticalmente con la etiqueta
    int barY = yBaseline - BAR_H + 1; // tope de la caja
    u8.drawFrame(BAR_X, barY, BAR_W, BAR_H);

    // Relleno proporcional al valor (1 px de margen interno)
    if (val > 0) {
        int fillW = (BAR_W - 2) * (int)val / 100;
        if (fillW > BAR_W - 2) fillW = BAR_W - 2;
        if (fillW > 0) {
            u8.drawBox(BAR_X + 1, barY + 1, fillW, BAR_H - 2);
        }
    }

    // Número alineado a la derecha con ancho fijo de 3 dígitos:
    // "%3u" rellena con espacios ("  5", " 42", "100") → columna estable
    // y el peor caso "100" entra completo en x=113..127.
    char numBuf[5];
    snprintf(numBuf, sizeof(numBuf), "%3u", (unsigned)val);
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
void menuRender(U8G2 &u8, const MenuData &d, uint8_t pagina) {
    if (pagina == 1) {
        // ── PÁGINA 1: Stats actual (diseño original) ──

        // ── HEADER IZQUIERDA: "espToy" + fecha (dos líneas chicas) ──
        // La zona x >= RELOJ_ZONA_X es exclusiva del reloj: estos textos
        // son cortos y nunca la alcanzan ("sab 12/07" = 9*5 = 45 px).
        if (d.horaValida) {
            // Línea 1: "espToy" en 4x6 (descendentes de 'p'/'y' llegan a y=6)
            u8.setFont(u8g2_font_4x6_tf);
            u8.drawStr(0, TITULO_Y, "espToy");

            // Línea 2: fecha "sab 12/07" en 5x8 (top en y=8, no pisa la línea 1)
            u8.setFont(u8g2_font_5x8_tf);
            int ds = (d.diaSemana >= 0 && d.diaSemana <= 6) ? d.diaSemana : 0;
            char fechaBuf[12];
            snprintf(fechaBuf, sizeof(fechaBuf), "%s %02d/%02d",
                     DIAS_SEMANA[ds], d.dia, d.mes);
            u8.drawStr(0, FECHA_Y, fechaBuf);
        } else {
            // Sin hora válida: solo "espToy", centrado verticalmente en el header
            u8.setFont(u8g2_font_5x8_tf);
            u8.drawStr(0, 11, "espToy");
        }

        // ── HEADER DERECHA: RELOJ HH:MM ──
        u8.setFont(u8g2_font_9x15B_tf);  // 9 px por char → "HH:MM" = 45 px
        char horaBuf[6];
        if (d.horaValida) {
            snprintf(horaBuf, sizeof(horaBuf), "%02d:%02d", d.hora, d.minuto);
        } else {
            snprintf(horaBuf, sizeof(horaBuf), "--:--");
        }
        // Alineado a la derecha dentro de su zona reservada (x≈83..127)
        int horaW = (int)u8.getStrWidth(horaBuf);
        u8.drawStr(SCREEN_W - horaW, HORA_Y, horaBuf);

        // ── LÍNEA SEPARADORA ──
        u8.drawHLine(0, SEP_Y, SCREEN_W);

        // ── FILA WIFI ──
        u8.setFont(u8g2_font_5x8_tf);

        char wifiBuf[32];
        if (d.portalActivo) {
            // Portal activo
            snprintf(wifiBuf, sizeof(wifiBuf), "WiFi: portal setup");
        } else if (d.wifiConfigurada && d.horaValida) {
            // WiFi conectada y hora sincronizada
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

        dibujarBarra(u8, "Feliz",    d.felicidad,    STAT_START_Y);
        dibujarBarra(u8, "Energia",  d.energia,      STAT_START_Y + STAT_STEP);
        dibujarBarra(u8, "Aburrim.", d.aburrimiento, STAT_START_Y + STAT_STEP * 2);
        // (Sin indicador "1/2": chocaría con el número de la tercera barra
        //  en x=113, y=53-60. La página 2 sí muestra "2/2".)

    } else if (pagina == 2) {
        // ── PÁGINA 2: Personalidad + edad ──

        // ── HEADER ──
        u8.setFont(u8g2_font_4x6_tf);
        u8.drawStr(0, TITULO_Y, "Personalidad");

        // Edad a la derecha: "N dias" o "en formacion"
        u8.setFont(u8g2_font_5x8_tf);
        char edadBuf[20];
        if (d.edadDias < 0) {
            snprintf(edadBuf, sizeof(edadBuf), "en formacion");
        } else if (d.edadDias < (int)PERSONALIDAD_DIAS_FORMACION) {
            snprintf(edadBuf, sizeof(edadBuf), "en formacion");
        } else {
            snprintf(edadBuf, sizeof(edadBuf), "%d dias", d.edadDias);
        }
        int edadW = (int)u8.getStrWidth(edadBuf);
        u8.drawStr(SCREEN_W - edadW - 2, FECHA_Y, edadBuf);

        // ── LÍNEA SEPARADORA ──
        u8.drawHLine(0, SEP_Y, SCREEN_W);

        // ── 4 LÍNEAS DE RASGOS ──
        // y = 27, 39, 51, 63 (cada 12 px entre baselines)
        u8.setFont(u8g2_font_5x8_tf);

        // Línea 1: Animo (alegre)
        const char* animoWord = elegirPalabraRasgo(d.alegre, "serio", "alegre", "muy alegre");
        char anim1[32];
        snprintf(anim1, sizeof(anim1), "Animo: %s", animoWord);
        u8.drawStr(0, 27, anim1);

        // Línea 2: Genio (grunon)
        const char* genioWord = elegirPalabraRasgo(d.grunon, "tranqui", "algo grunon", "muy grunon");
        char anim2[32];
        snprintf(anim2, sizeof(anim2), "Genio: %s", genioWord);
        u8.drawStr(0, 39, anim2);

        // Línea 3: Energia (energetico)
        const char* enerWord = elegirPalabraRasgo(d.energetico, "calmo", "activo", "muy activo");
        char anim3[32];
        snprintf(anim3, sizeof(anim3), "Energia: %s", enerWord);
        u8.drawStr(0, 51, anim3);

        // Línea 4: Vagueza (perezoso)
        const char* vagWord = elegirPalabraRasgo(d.perezoso, "inquieto", "algo vago", "muy vago");
        char anim4[32];
        snprintf(anim4, sizeof(anim4), "Vagueza: %s", vagWord);
        u8.drawStr(0, 63, anim4);

        // ── Indicador de página "2/3" ──
        u8.setFont(u8g2_font_5x8_tf);
        u8.drawStr(SCREEN_W - 15, SCREEN_H - 1, "2/3");

    } else if (pagina == 3) {
        // ── PÁGINA 3: WiFi + firmware ──

        // ── HEADER ──
        u8.setFont(u8g2_font_4x6_tf);
        u8.drawStr(0, TITULO_Y, "WiFi y firmware");

        // "3/3" alineado a la derecha en la misma línea del título
        // Con fuente 4x6: "3/3" = 3 chars × 4 px = 12 px
        u8.drawStr(SCREEN_W - 12, TITULO_Y, "3/3");

        // ── LÍNEA SEPARADORA ──
        u8.drawHLine(0, SEP_Y, SCREEN_W);

        // ── LÍNEAS DE INFORMACIÓN ──
        u8.setFont(u8g2_font_5x8_tf);

        // Línea 1 (y=27): "Red: <ssid> <estado>"
        // Estado: "OK" si staConectada; "portal" si portalActivo; "s/conex" si no.
        // El SSID se trunca para que la línea entera entre en 128 px.
        // Ancho fijo por estado: "OK"=2ch, "portal"=6ch, "s/conex"=7ch → usamos 7 como peor caso.
        // "Red: " = 5ch; estado máx = 7ch + 1 espacio; queda: 128/5 - 5 - 1 - 7 = 12 chars para ssid.
        {
            const char* estado;
            if (d.staConectada)   estado = "OK";
            else if (d.portalActivo) estado = "portal";
            else                  estado = "s/conex";

            char ssidTrunc[13];  // 12 chars + '\0'
            truncarSSID(ssidTrunc, d.ssid ? d.ssid : "", 12);

            char redBuf[36];
            snprintf(redBuf, sizeof(redBuf), "Red: %s %s", ssidTrunc, estado);
            u8.drawStr(0, 27, redBuf);
        }

        // Línea 2 (y=39): "FW: v<fwVersion>"
        {
            char fwBuf[24];
            snprintf(fwBuf, sizeof(fwBuf), "FW: v%s", d.fwVersion ? d.fwVersion : "?");
            u8.drawStr(0, 39, fwBuf);
        }

        // Línea 3 (y=51): estado de actualización
        {
            if (d.hayUpdate) {
                char updBuf[28];
                snprintf(updBuf, sizeof(updBuf), "Nueva: v%s!", d.versionNueva ? d.versionNueva : "?");
                u8.drawStr(0, 51, updBuf);
            } else {
                u8.drawStr(0, 51, "Firmware al dia");
            }
        }

        // Línea 4 (y=63): estado de sonido + hint de acción
        // Con fuente 4x6 (4 px/char): cabe ~32 chars en 128 px.
        // "son:off  cabeza:WiFi pie:actlz" = 30 chars → entra.
        u8.setFont(u8g2_font_4x6_tf);
        {
            const char* sonStr = d.sonidoHabilitado ? "on " : "off";
            if (d.hayUpdate) {
                char buf[36];
                snprintf(buf, sizeof(buf), "son:%s  cab:WiFi pie:actualiz", sonStr);
                u8.drawStr(0, 63, buf);
            } else {
                char buf[36];
                snprintf(buf, sizeof(buf), "son:%s  mantener:toggle son.", sonStr);
                u8.drawStr(0, 63, buf);
            }
        }
    }
}
