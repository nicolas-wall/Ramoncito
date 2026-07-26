// =============================================================
//  Ramoncito — presence.h
//  Presencia por WiFi: sondea por ARP la IP del teléfono del dueño en
//  la LAN. El chip WiFi del teléfono responde ARP aun dormido, así que
//  es más confiable que ping. Llegada rápida, "ausente" con gracia.
//  La IP objetivo se vincula desde el panel web (guardada en NVS).
// =============================================================
#pragma once
#include <Arduino.h>

class Presence {
public:
    void begin();                 // carga la IP objetivo de NVS
    void update(uint32_t now);    // sondea por ARP y actualiza el estado
    bool configurado() const { return _target != 0; }
    bool presente()    const { return _presente; }

    // One-shots de transición (los consume main.cpp)
    bool justArrived();
    bool justLeft();

    // Guarda la IP a vigilar (desde el panel: la del cliente HTTP).
    // Devuelve false si la IP no es válida.
    bool setTargetIP(const char* ip);
    const char* targetIP() const { return _ipStr; }

private:
    char     _ipStr[16] = {0};
    uint32_t _target    = 0;    // ip4 addr (mismo formato que lwIP)
    bool     _presente  = false;
    uint32_t _lastSeen  = 0;
    uint32_t _lastProbe = 0;
    bool     _flagArrived = false;
    bool     _flagLeft    = false;

    bool _probe();              // envía ARP request + consulta la tabla ARP
};

extern Presence presence;
