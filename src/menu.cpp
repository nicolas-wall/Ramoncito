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
    // ── OVERLAY: confirmación de renacer (pantalla completa, cualquier página) ──
    if (d.renacerConfirmando) {
        u8.setFont(u8g2_font_9x15B_tf);
        int rw = (int)u8.getStrWidth("RENACER?");
        u8.drawStr((SCREEN_W - rw) / 2, 28, "RENACER?");

        u8.setFont(u8g2_font_5x8_tf);
        const char* linea2 = "borra todo";
        int l2w = (int)u8.getStrWidth(linea2);
        u8.drawStr((SCREEN_W - l2w) / 2, 42, linea2);

        u8.setFont(u8g2_font_4x6_tf);
        const char* linea3 = "pie=confirmar btn=cancelar";
        int l3w = (int)u8.getStrWidth(linea3);
        u8.drawStr((SCREEN_W - l3w) / 2, 56, linea3);
        return;
    }

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
        // ── PÁGINA 2: Personalidad (2 ejes) + edad ──

        // ── HEADER ──
        u8.setFont(u8g2_font_4x6_tf);
        u8.drawStr(0, TITULO_Y, "Personalidad");

        // Edad a la derecha: siempre muestra el conteo de días.
        // Solo si <0 (sin hora válida aún) muestra "s/edad".
        // "*" indica que todavía está en período de formación.
        u8.setFont(u8g2_font_5x8_tf);
        char edadBuf[20];
        if (d.edadDias < 0) {
            snprintf(edadBuf, sizeof(edadBuf), "s/edad");
        } else if (d.edadDias == 1) {
            bool enForm = (d.edadDias < (int)PERSONALIDAD_DIAS_FORMACION);
            snprintf(edadBuf, sizeof(edadBuf), "1 dia%s", enForm ? "*" : "");
        } else {
            bool enForm = (d.edadDias < (int)PERSONALIDAD_DIAS_FORMACION);
            snprintf(edadBuf, sizeof(edadBuf), "%d dias%s", d.edadDias, enForm ? "*" : "");
        }
        int edadW = (int)u8.getStrWidth(edadBuf);
        u8.drawStr(SCREEN_W - edadW - 2, FECHA_Y, edadBuf);

        // ── LÍNEA SEPARADORA ──
        u8.drawHLine(0, SEP_Y, SCREEN_W);

        // ── 2 EJES BIPOLARES ──
        u8.setFont(u8g2_font_5x8_tf);

        // Eje ÁNIMO (0=gruñón .. 100=alegre) → d.alegre
        const char* animoWord = elegirPalabraRasgo(d.alegre, "grunon", "normal", "alegre");
        char anim1[32];
        snprintf(anim1, sizeof(anim1), "Animo: %s", animoWord);
        u8.drawStr(0, 32, anim1);

        // Eje ENERGÍA (0=perezoso .. 100=energético) → d.energetico
        const char* enerWord = elegirPalabraRasgo(d.energetico, "tranqui", "activo", "muy activo");
        char anim2[32];
        snprintf(anim2, sizeof(anim2), "Energia: %s", enerWord);
        u8.drawStr(0, 48, anim2);

        // ── Indicador de página "2/4" ──
        u8.setFont(u8g2_font_4x6_tf);
        u8.drawStr(SCREEN_W - 12, SCREEN_H - 1, "2/4");

    } else if (pagina == 3) {
        // ── PÁGINA 3: WiFi + firmware ──

        // ── HEADER ──
        u8.setFont(u8g2_font_4x6_tf);
        u8.drawStr(0, TITULO_Y, "WiFi y firmware");

        // "3/4" alineado a la derecha en la misma línea del título
        // Con fuente 4x6: "3/4" = 3 chars × 4 px = 12 px
        u8.drawStr(SCREEN_W - 12, TITULO_Y, "3/4");

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

        // Línea 4 (y=63): hint de acción OTA (solo si hay update)
        u8.setFont(u8g2_font_4x6_tf);
        if (d.hayUpdate) {
            u8.drawStr(0, 63, "pie: instalar actualizacion");
        }

    } else if (pagina == 4) {
        // ── PÁGINA 4: Ajustes (sonido / renacer / cambiar WiFi) ──

        // ── HEADER ──
        u8.setFont(u8g2_font_4x6_tf);
        u8.drawStr(0, TITULO_Y, "AJUSTES");
        u8.drawStr(SCREEN_W - 12, TITULO_Y, "4/4");

        // ── LÍNEA SEPARADORA ──
        u8.drawHLine(0, SEP_Y, SCREEN_W);

        // ── 3 OPCIONES CON CURSOR ──
        // El cursor ">" marca la opción resaltada (d.ajustesSel).
        u8.setFont(u8g2_font_5x8_tf);
        const int OPT_Y0   = 27;   // baseline primera opción
        const int OPT_STEP = 11;

        // Opción 0: Sonido on/off
        {
            char buf[24];
            snprintf(buf, sizeof(buf), "%cSonido: %s",
                     d.ajustesSel == 0 ? '>' : ' ',
                     d.sonidoHabilitado ? "on" : "off");
            u8.drawStr(0, OPT_Y0, buf);
        }
        // Opción 1: Renacer
        {
            char buf[24];
            snprintf(buf, sizeof(buf), "%cRenacer",
                     d.ajustesSel == 1 ? '>' : ' ');
            u8.drawStr(0, OPT_Y0 + OPT_STEP, buf);
        }
        // Opción 2: Cambiar WiFi
        {
            char buf[24];
            snprintf(buf, sizeof(buf), "%cCambiar WiFi",
                     d.ajustesSel == 2 ? '>' : ' ');
            u8.drawStr(0, OPT_Y0 + OPT_STEP * 2, buf);
        }

        // ── Hint inferior ──
        u8.setFont(u8g2_font_4x6_tf);
        u8.drawStr(0, SCREEN_H - 1, "cabeza:mover  pie:elegir");
    }
}
