// =============================================================
//  Ramoncito — telegram.h
//  Mensajería de ida y vuelta por la Bot API de Telegram (HTTPS,
//  sin servidor propio). TODA la red corre en una TAREA DE FONDO
//  pinneada al core 0, para que el loop de la cara (core 1) NUNCA
//  se bloquee por una llamada HTTPS lenta. main.cpp solo intercambia
//  datos por colas/mutex (no bloquea). Solo obedece a TELEGRAM_CHAT_ID.
// =============================================================
#pragma once
#include <Arduino.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

class Telegram {
public:
    void begin();                     // arranca la tarea de red (core 0)
    bool habilitado()  const { return _on; }
    bool silenciado()  const { return _silencio; }   // /callar activo → sin proactivos

    // Encola un texto para enviar. NO bloquea: lo manda la tarea de fondo.
    // Devuelve true si se encoló (false si desactivado o cola llena).
    bool enviar(const char* texto);

    // Comando recibido, para que main.cpp lo ejecute (tiene acceso a mood/sound).
    enum class Cmd : uint8_t { NINGUNO, ESTADO, FELIZ, SONIDO };
    Cmd tomarComando();

    // Toma (copia + limpia) la pregunta de texto libre pendiente; main la
    // responde en personaje. Devuelve false si no hay.
    bool tomarMensaje(char* out, size_t n);

    // Toma (copia + limpia) el texto de ":::" para mostrar en pantalla; main
    // hace el notify.push (así la cola de avisos queda en un solo hilo).
    bool tomarMostrar(char* out, size_t n);

private:
    bool          _on       = false;
    volatile bool _silencio = false;
    String        _token, _chatId;
    uint32_t      _lastPoll = 0;
    int64_t       _offset   = 0;         // update_id + 1 para el próximo getUpdates
    Cmd           _cmd      = Cmd::NINGUNO;
    char          _mensaje[161] = {0};   // pregunta de texto libre (la responde main)
    char          _mostrar[161] = {0};   // texto de ":::" (lo muestra main)

    QueueHandle_t     _sendQ = nullptr;  // cola de textos a enviar (main/task → task)
    SemaphoreHandle_t _mtx   = nullptr;  // protege _cmd/_mensaje/_mostrar

    static void _taskTrampoline(void* arg);
    void   _task();                      // loop de la tarea de fondo (core 0)
    void   _poll();                      // getUpdates (bloqueante, en la tarea)
    void   _sendNow(const char* texto);  // sendMessage (bloqueante, en la tarea)
    void   _procesar(const char* texto); // clasifica un mensaje entrante
    String _urlEncode(const char* s);
};

extern Telegram telegram;
