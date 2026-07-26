// =============================================================
//  Ramoncito — telegram.cpp
// =============================================================
#include "telegram.h"
#include "config.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>

// Token/chat id desde secrets.h (si existe). Con __has_include el repo
// compila aunque falte secrets.h (defaults vacíos → Telegram desactivado).
#if __has_include("secrets.h")
  #include "secrets.h"
#endif
#ifndef TELEGRAM_BOT_TOKEN
  #define TELEGRAM_BOT_TOKEN ""
#endif
#ifndef TELEGRAM_CHAT_ID
  #define TELEGRAM_CHAT_ID ""
#endif

Telegram telegram;

static const char* TG_HOST = "https://api.telegram.org";

void Telegram::begin() {
    _token  = String(TELEGRAM_BOT_TOKEN);
    _chatId = String(TELEGRAM_CHAT_ID);
    _on = TELEGRAM_HABILITADO && _token.length() > 0 && _chatId.length() > 0;
    if (!_on) { Serial.println("[tg] Telegram desactivado (sin token en secrets.h)"); return; }

    _mtx   = xSemaphoreCreateMutex();
    _sendQ = xQueueCreate(6, 256);   // hasta 6 textos de 256 bytes en cola
    // Tarea de red en el CORE 0 (el loop de Arduino corre en el core 1), con
    // stack grande porque el handshake TLS de mbedtls consume bastante.
    xTaskCreatePinnedToCore(_taskTrampoline, "tg_net", 16384, this, 1, nullptr, 0);
    Serial.println("[tg] Telegram habilitado (tarea de red en core 0)");
}

// ----- Getters thread-safe (los llama main en el core 1) ------
Telegram::Cmd Telegram::tomarComando() {
    if (!_mtx) return Cmd::NINGUNO;
    Cmd c;
    xSemaphoreTake(_mtx, portMAX_DELAY);
    c = _cmd; _cmd = Cmd::NINGUNO;
    xSemaphoreGive(_mtx);
    return c;
}
bool Telegram::tomarMensaje(char* out, size_t n) {
    if (!_mtx) return false;
    bool hay = false;
    xSemaphoreTake(_mtx, portMAX_DELAY);
    if (_mensaje[0]) { snprintf(out, n, "%s", _mensaje); _mensaje[0] = 0; hay = true; }
    xSemaphoreGive(_mtx);
    return hay;
}
bool Telegram::tomarMostrar(char* out, size_t n) {
    if (!_mtx) return false;
    bool hay = false;
    xSemaphoreTake(_mtx, portMAX_DELAY);
    if (_mostrar[0]) { snprintf(out, n, "%s", _mostrar); _mostrar[0] = 0; hay = true; }
    xSemaphoreGive(_mtx);
    return hay;
}

// ----- Envío: solo encola; el envío real lo hace la tarea -----
bool Telegram::enviar(const char* texto) {
    if (!_on || !_sendQ) return false;
    char buf[256];
    snprintf(buf, sizeof(buf), "%s", texto);
    return xQueueSend(_sendQ, buf, 0) == pdTRUE;
}

// ----- Tarea de fondo (core 0): drena envíos + poll de entrantes
void Telegram::_taskTrampoline(void* arg) { ((Telegram*)arg)->_task(); }

void Telegram::_task() {
    char msg[256];
    for (;;) {
        if (WiFi.status() == WL_CONNECTED) {
            // 1) Enviar todo lo encolado
            while (xQueueReceive(_sendQ, msg, 0) == pdTRUE) _sendNow(msg);
            // 2) Revisar mensajes entrantes cada TELEGRAM_POLL_MS
            uint32_t now = millis();
            if (_lastPoll == 0 || (now - _lastPoll) >= TELEGRAM_POLL_MS) {
                _lastPoll = now;
                _poll();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

// ----- sendMessage (bloqueante, SOLO dentro de la tarea) ------
void Telegram::_sendNow(const char* texto) {
    if (WiFi.status() != WL_CONNECTED) return;
    String url = String(TG_HOST) + "/bot" + _token + "/sendMessage?chat_id=" + _chatId
               + "&text=" + _urlEncode(texto);

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(6);   // acota el handshake TLS (lo lento)
    client.setTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(6000);
    if (!http.begin(client, url)) return;
    int code = http.GET();
    http.end();
    if (code != 200) Serial.printf("[tg] envio fallo (HTTP %d)\n", code);
}

// ----- getUpdates (bloqueante, SOLO dentro de la tarea) -------
void Telegram::_poll() {
    String url = String(TG_HOST) + "/bot" + _token + "/getUpdates?limit=3&timeout=0";
    if (_offset) {
        char off[24];
        snprintf(off, sizeof(off), "%lld", (long long)_offset);
        url += "&offset="; url += off;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setHandshakeTimeout(6);   // acota el handshake TLS (lo lento)
    client.setTimeout(8);
    HTTPClient http;
    http.setConnectTimeout(6000);
    if (!http.begin(client, url)) return;
    int code = http.GET();
    if (code != 200) { http.end(); return; }

    // Filtro: solo los campos que usamos (ahorra RAM en el parseo).
    JsonDocument filtro;
    filtro["result"][0]["update_id"] = true;
    filtro["result"][0]["message"]["text"]    = true;
    filtro["result"][0]["message"]["chat"]["id"] = true;

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream(),
                                               DeserializationOption::Filter(filtro));
    http.end();
    if (err) { Serial.printf("[tg] json err: %s\n", err.c_str()); return; }

    int64_t want = (int64_t)atoll(_chatId.c_str());
    for (JsonObject upd : doc["result"].as<JsonArray>()) {
        int64_t uid = upd["update_id"].as<long long>();
        _offset = uid + 1;   // confirmar: no volver a recibir este update
        const char* texto = upd["message"]["text"] | "";
        int64_t chat = upd["message"]["chat"]["id"].as<long long>();
        // Whitelist: solo el dueño (comparación en 64 bits)
        if (chat != want) {
            Serial.printf("[tg] mensaje de chat no autorizado (%lld) ignorado\n", (long long)chat);
            continue;
        }
        if (texto[0]) _procesar(texto);
    }
}

// ----- Procesar un mensaje entrante ---------------------------
void Telegram::_procesar(const char* texto) {
    String t(texto); t.trim();
    Serial.printf("[tg] recibido: %s\n", t.c_str());

    // ":::" al inicio → mostrar el texto que sigue en la pantalla del toy.
    // Se lo pasamos a main (por _mostrar) para que él haga el notify.push
    // (así la cola de avisos la toca un solo hilo, el del loop).
    if (t.startsWith(":::")) {
        String resto = t.substring(3);
        resto.trim();
        if (resto.length() == 0) enviar("mandame el texto asi: ::: hola!");
        else {
            xSemaphoreTake(_mtx, portMAX_DELAY);
            snprintf(_mostrar, sizeof(_mostrar), "%s", resto.c_str());
            xSemaphoreGive(_mtx);
            enviar("listo, lo muestro en mi pantalla 📺");
        }
        return;
    }

    if (t.startsWith("/")) {
        String cmd = t; int sp = cmd.indexOf(' '); if (sp > 0) cmd = cmd.substring(0, sp);
        cmd.toLowerCase();
        if (cmd == "/estado") {
            xSemaphoreTake(_mtx, portMAX_DELAY); _cmd = Cmd::ESTADO; xSemaphoreGive(_mtx);
        } else if (cmd == "/feliz") {
            xSemaphoreTake(_mtx, portMAX_DELAY); _cmd = Cmd::FELIZ;  xSemaphoreGive(_mtx);
            enviar("dale! 😄");
        } else if (cmd == "/sonido") {
            xSemaphoreTake(_mtx, portMAX_DELAY); _cmd = Cmd::SONIDO; xSemaphoreGive(_mtx);
        } else if (cmd == "/callar") {
            _silencio = true;  enviar("ok, me callo un rato 🤫");
        } else if (cmd == "/hablar") {
            _silencio = false; enviar("volví! 🐹");
        } else { // /help /start u otro
            enviar("Soy Ramoncito 🐹\n"
                   "Haceme una pregunta y te contesto!\n"
                   "Ej: como estas? tenes hambre? contame un chiste\n\n"
                   "::: <texto> - lo muestro en mi pantalla\n"
                   "/estado - como me siento\n"
                   "/feliz - ponerme contento\n"
                   "/sonido - mute on/off\n"
                   "/callar - no te escribo\n"
                   "/hablar - vuelvo a escribir");
        }
        return;
    }

    // Texto libre: lo guardamos para que main lo responda en personaje.
    xSemaphoreTake(_mtx, portMAX_DELAY);
    snprintf(_mensaje, sizeof(_mensaje), "%s", t.c_str());
    xSemaphoreGive(_mtx);
}

// ----- URL-encode ---------------------------------------------
String Telegram::_urlEncode(const char* s) {
    String out; out.reserve(strlen(s) * 3);
    const char* hex = "0123456789ABCDEF";
    for (const char* p = s; *p; p++) {
        unsigned char c = (unsigned char)*p;
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char)c;
        } else {
            out += '%'; out += hex[c >> 4]; out += hex[c & 0x0F];
        }
    }
    return out;
}
