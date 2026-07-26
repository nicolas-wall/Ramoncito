// =============================================================
//  Ramoncito — telegram.cpp
// =============================================================
#include "telegram.h"
#include "config.h"
#include "notify.h"

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
    if (_on) Serial.println("[tg] Telegram habilitado (token presente)");
    else     Serial.println("[tg] Telegram desactivado (sin token en secrets.h)");
}

Telegram::Cmd Telegram::tomarComando() {
    Cmd c = _cmd; _cmd = Cmd::NINGUNO; return c;
}

// ----- Envío --------------------------------------------------
bool Telegram::enviar(const char* texto) {
    if (!_on || WiFi.status() != WL_CONNECTED) return false;
    uint32_t now = millis();
    if (_lastSend != 0 && (now - _lastSend) < TELEGRAM_MIN_ENVIO_MS) return false;
    _lastSend = now;

    String url = String(TG_HOST) + "/bot" + _token + "/sendMessage?chat_id=" + _chatId
               + "&text=" + _urlEncode(texto);

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(12);
    HTTPClient http;
    if (!http.begin(client, url)) return false;
    int code = http.GET();
    http.end();
    if (code != 200) Serial.printf("[tg] envio fallo (HTTP %d)\n", code);
    return code == 200;
}

// ----- Poll de entrantes --------------------------------------
void Telegram::update(uint32_t now, bool ocupado) {
    if (!_on || ocupado || WiFi.status() != WL_CONNECTED) return;
    if (_lastPoll != 0 && (now - _lastPoll) < TELEGRAM_POLL_MS) return;
    _lastPoll = now;
    _poll();
}

void Telegram::_poll() {
    String url = String(TG_HOST) + "/bot" + _token + "/getUpdates?limit=3&timeout=0";
    if (_offset) {
        char off[24];
        snprintf(off, sizeof(off), "%lld", (long long)_offset);
        url += "&offset="; url += off;
    }

    WiFiClientSecure client;
    client.setInsecure();
    client.setTimeout(10);
    HTTPClient http;
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

    if (t.startsWith("/")) {
        String cmd = t; int sp = cmd.indexOf(' '); if (sp > 0) cmd = cmd.substring(0, sp);
        cmd.toLowerCase();
        if (cmd == "/estado")      { _cmd = Cmd::ESTADO; }
        else if (cmd == "/feliz")  { _cmd = Cmd::FELIZ;  enviar("dale! 😄"); }
        else if (cmd == "/sonido") { _cmd = Cmd::SONIDO; }
        else if (cmd == "/callar") { _silencio = true;  enviar("ok, me callo un rato 🤫"); }
        else if (cmd == "/hablar") { _silencio = false; enviar("volví! 🐹"); }
        else if (cmd == "/mostrar" || cmd == "/pantalla") {
            // Muestra el texto que sigue en la pantalla del toy
            String resto = (sp > 0) ? t.substring(sp + 1) : "";
            resto.trim();
            if (resto.length() == 0) enviar("mandame el texto asi: /mostrar hola!");
            else {
                notify.push("Mensaje", resto.c_str(), NotifIcon::CHAT);
                enviar("listo, lo muestro en mi pantalla 📺");
            }
        }
        else { // /help /start u otro
            enviar("Soy Ramoncito 🐹\n"
                   "Haceme una pregunta y te contesto!\n"
                   "Ej: como estas? tenes hambre? contame un chiste\n\n"
                   "/mostrar <texto> - lo muestro en mi pantalla\n"
                   "/estado - como me siento\n"
                   "/feliz - ponerme contento\n"
                   "/sonido - mute on/off\n"
                   "/callar - no te escribo\n"
                   "/hablar - vuelvo a escribir");
        }
        return;
    }

    // Texto libre: lo guardamos para que main lo responda en personaje
    // (main tiene acceso a humor/personalidad/hora). No respondemos acá.
    snprintf(_mensaje, sizeof(_mensaje), "%s", t.c_str());
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
