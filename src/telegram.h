// =============================================================
//  Ramoncito — telegram.h
//  Mensajería de ida y vuelta por la Bot API de Telegram (HTTPS,
//  sin servidor propio). Ramoncito te escribe (enviar) y revisa
//  mensajes entrantes (update, polling throttled). Solo obedece al
//  chat de TELEGRAM_CHAT_ID. Token/chat id vienen de secrets.h;
//  con token vacío queda desactivado.
// =============================================================
#pragma once
#include <Arduino.h>

class Telegram {
public:
    void begin();
    bool habilitado()  const { return _on; }
    bool silenciado()  const { return _silencio; }   // /callar activo → sin proactivos

    // Poll de mensajes entrantes. 'ocupado' = true cuando el toy está en una
    // reacción/menú/animación: se salta el poll para no cortar la animación
    // (la llamada HTTPS bloquea unos cientos de ms).
    void update(uint32_t now, bool ocupado);

    // Envía un mensaje al chat. Rate-limited. No hace nada si está desactivado
    // o sin WiFi. Devuelve true si se intentó el envío.
    bool enviar(const char* texto);

    // Comando recibido, para que main.cpp lo ejecute (tiene acceso a mood/sound).
    enum class Cmd : uint8_t { NINGUNO, ESTADO, FELIZ, SONIDO };
    Cmd tomarComando();

    // Mensaje de texto libre (no-comando) para que main.cpp lo responda en
    // personaje (main tiene acceso a mood/personalidad/hora). Vacío = nada.
    bool        hayMensaje() const { return _mensaje[0] != 0; }
    const char* mensaje()    const { return _mensaje; }
    void        limpiarMensaje()   { _mensaje[0] = 0; }

private:
    bool     _on       = false;
    bool     _silencio = false;
    String   _token, _chatId;
    uint32_t _lastPoll = 0;
    uint32_t _lastSend = 0;
    int64_t  _offset   = 0;      // update_id + 1 para el próximo getUpdates (64 bits)
    Cmd      _cmd      = Cmd::NINGUNO;
    char     _mensaje[161] = {0};  // último texto libre recibido (lo responde main)

    void _poll();
    void _procesar(const char* texto);
    String _urlEncode(const char* s);
};

extern Telegram telegram;
