#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

// Datos que main.cpp junta cada frame para el menú
struct MenuData {
    uint8_t felicidad;      // 0-100
    uint8_t energia;        // 0-100
    uint8_t aburrimiento;   // 0-100
    bool    horaValida;
    int     hora;           // 0-23 (válido solo si horaValida)
    int     minuto;         // 0-59 (ídem)
    bool    wifiConfigurada;   // hay SSID guardado
    const char* ssid;          // SSID guardado ("" si no hay)
    bool    portalActivo;
    const char* fwVersion;
};

// Dibuja la pantalla de estado completa (no llama clearBuffer/sendBuffer)
void menuRender(U8G2 &u8, const MenuData &d);
