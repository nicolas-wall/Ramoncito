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
    int     dia;            // 1-31 (ídem)
    int     mes;            // 1-12 (ídem)
    int     diaSemana;      // 0=domingo, como tm_wday (ídem)
    bool    wifiConfigurada;   // hay SSID guardado
    const char* ssid;          // SSID guardado ("" si no hay)
    bool    portalActivo;
    // Rasgos de personalidad (Etapa C)
    uint8_t alegre;         // 0-100
    uint8_t grunon;         // 0-100
    uint8_t energetico;     // 0-100
    uint8_t perezoso;       // 0-100
    int     edadDias;       // -1 si sin hora válida; < PERSONALIDAD_DIAS_FORMACION si en formación
};

// Dibuja la pantalla de estado completa (no llama clearBuffer/sendBuffer)
// pagina: 1 = stats actual, 2 = personalidad + edad
void menuRender(U8G2 &u8, const MenuData &d, uint8_t pagina);
