// =============================================================
//  Ramoncito — notify.h
//  Motor de notificaciones en pantalla: cola de avisos (ícono +
//  título + texto). main.cpp los saca de a uno y los muestra como
//  un estado de pantalla (AppState::NOTIF), sonando y encendiéndose
//  desde standby. Fuentes: Telegram y el webhook POST /api/notify.
// =============================================================
#pragma once
#include <Arduino.h>
#include <U8g2lib.h>

enum class NotifIcon : uint8_t { CHAT, MAIL, BELL, ALERTA, RELOJ };

struct Notif {
    char      titulo[24];
    char      texto[161];
    NotifIcon icono;
};

class Notify {
public:
    // Encola un aviso (thread-safe básico: lo llaman el loop y el server web,
    // ambos en el mismo core/hilo Arduino, así que no hay concurrencia real).
    // Si la cola está llena, descarta el más viejo.
    void push(const char* titulo, const char* texto, NotifIcon icono);

    // Helper: parsea un id de ícono textual del webhook ("chat","mail",...).
    static NotifIcon iconoDeTexto(const char* s);

    bool hayPendiente() const { return _count > 0; }

    // Saca el siguiente aviso a mostrar. Devuelve false si no hay.
    bool pop(Notif& out);

    // Dibuja la tarjeta del aviso (no llama clear/sendBuffer). mostradaDesde
    // = millis() cuando empezó a mostrarse (para el scroll del texto largo).
    void render(U8G2 &u8, const Notif &n, uint32_t ahora, uint32_t mostradaDesde);

private:
    static const uint8_t CAP = 5;
    Notif   _q[CAP];
    uint8_t _head = 0;    // índice del próximo a sacar
    uint8_t _count = 0;   // cuántos hay en cola

    void _drawIcono(U8G2 &u8, NotifIcon ic, int x, int y);  // 16x16 aprox en (x,y) top-left
};

extern Notify notify;
