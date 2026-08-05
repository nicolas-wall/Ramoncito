#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// ============================================================
//  menu.h — Menú de sistema del arcade
//
//  Quedó reducido a lo que un aparato de escritorio necesita: saber la
//  hora, si tiene red, qué firmware corre, y poder tocar las dos únicas
//  preferencias que hay. Las páginas de stats de humor y de personalidad
//  se fueron con el pivot a arcade — medían cosas que ya no existen.
// ============================================================

// Datos que main.cpp junta cada frame para el menú
struct MenuData {
    bool    horaValida;
    int     hora;           // 0-23 (válido solo si horaValida)
    int     minuto;         // 0-59 (ídem)
    int     dia;            // 1-31 (ídem)
    int     mes;            // 1-12 (ídem)
    int     diaSemana;      // 0=domingo, como tm_wday (ídem)

    bool        wifiConfigurada;  // hay SSID guardado
    const char* ssid;             // SSID guardado ("" si no hay)
    bool        portalActivo;
    bool        staConectada;     // STA conectada a un AP
    const char* lanIP;            // IP en la LAN ("" si no conectada)

    const char* fwVersion;        // versión actual ("0.11.0")
    bool        hayUpdate;        // hay actualización disponible
    const char* versionNueva;     // versión nueva ("" si no hay)

    bool    sonidoHabilitado;
    uint8_t ajustesSel;           // opción resaltada en Ajustes (0=Sonido, 1=WiFi)
};

// Cantidad de páginas y de opciones en Ajustes. main.cpp los usa para
// paginar y para mover el cursor sin repetir los números.
static const uint8_t MENU_PAGINAS      = 2;   // 1 = info, 2 = ajustes
static const uint8_t MENU_AJUSTES_OPTS = 2;   // Sonido, Cambiar WiFi

// Dibuja la pantalla completa (no llama clearBuffer/sendBuffer)
// pagina: 1 = info (hora, red, firmware), 2 = ajustes
void menuRender(U8G2 &u8, const MenuData &d, uint8_t pagina);
