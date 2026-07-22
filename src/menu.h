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
    // Campos de firmware / OTA (página 3)
    const char* fwVersion;    // versión actual ("0.9.0")
    bool    staConectada;     // STA conectada a un AP
    bool    hayUpdate;        // hay actualización disponible
    const char* versionNueva; // versión nueva ("" si no hay)
    bool    sonidoHabilitado; // estado del sonido (página 4 · Ajustes)
    // Panel web en la LAN (página 3): dirección para conectarse desde el teléfono
    bool    lanServerActivo;  // true = el dashboard está escuchando en la LAN
    const char* lanIP;        // IP del toy en la LAN ("" si no conectado)
    // Personalidad — 2 ejes bipolares (Etapa C); alegre=animo, energetico=energia
    uint8_t alegre;         // 0-100 = eje ÁNIMO (0 gruñón .. 100 alegre)
    uint8_t energetico;     // 0-100 = eje ENERGÍA (0 perezoso .. 100 energético)
    int     edadDias;       // -1 si sin hora válida; < PERSONALIDAD_DIAS_FORMACION si en formación

    // Sub-estado de confirmación de renacer (overlay, cualquier página)
    bool    renacerConfirmando;  // true = esperando confirmación del pie

    // Página 4 · Ajustes: opción resaltada (0=Sonido, 1=Renacer, 2=Cambiar WiFi)
    uint8_t ajustesSel;
};

// Dibuja la pantalla de estado completa (no llama clearBuffer/sendBuffer)
// pagina: 1 = stats, 2 = personalidad + edad, 3 = WiFi + firmware, 4 = ajustes
void menuRender(U8G2 &u8, const MenuData &d, uint8_t pagina);
