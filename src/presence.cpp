// =============================================================
//  Ramoncito — presence.cpp
// =============================================================
#include "presence.h"
#include "config.h"

#include <WiFi.h>
#include <Preferences.h>
#include "lwip/etharp.h"
#include "lwip/netif.h"

Presence presence;

// Encuentra el netif de la STA (el que tiene la IP de WiFi.localIP()).
// En AP_STA el netif_default puede ser el AP, por eso lo buscamos por IP.
static struct netif* staNetif() {
    uint32_t self = (uint32_t)WiFi.localIP();
    if (self == 0) return nullptr;
    for (struct netif* n = netif_list; n != nullptr; n = n->next) {
        const ip4_addr_t* a = netif_ip4_addr(n);
        if (a && a->addr == self) return n;
    }
    return netif_default;
}

void Presence::begin() {
    Preferences prefs;
    prefs.begin("ramoncito", true);
    String ip = prefs.getString("phoneIP", "");
    prefs.end();
    if (ip.length() > 0) {
        setTargetIP(ip.c_str());
        Serial.printf("[pres] vigilando teléfono en %s\n", _ipStr);
    } else {
        Serial.println("[pres] sin teléfono vinculado (vincular desde el panel)");
    }
}

bool Presence::setTargetIP(const char* ip) {
    IPAddress parsed;
    if (!parsed.fromString(ip)) return false;
    uint32_t v = (uint32_t)parsed;
    if (v == 0) return false;
    _target = v;
    snprintf(_ipStr, sizeof(_ipStr), "%s", parsed.toString().c_str());
    // Reset del estado para re-evaluar contra el nuevo objetivo
    _presente = false; _lastSeen = 0; _lastProbe = 0;
    Preferences prefs;
    prefs.begin("ramoncito", false);
    prefs.putString("phoneIP", _ipStr);
    prefs.end();
    Serial.printf("[pres] teléfono vinculado: %s\n", _ipStr);
    return true;
}

bool Presence::justArrived() { bool f = _flagArrived; _flagArrived = false; return f; }
bool Presence::justLeft()    { bool f = _flagLeft;    _flagLeft    = false; return f; }

// Envía un ARP request por la IP objetivo y consulta si hay una entrada
// estable en la tabla ARP (el teléfono respondió en algún momento reciente;
// lwIP mantiene la entrada mientras el equipo siga contestando).
bool Presence::_probe() {
    struct netif* nif = staNetif();
    if (!nif) return false;
    ip4_addr_t tgt; tgt.addr = _target;

    etharp_request(nif, &tgt);   // no bloquea: dispara el who-has

    struct eth_addr*  eth = nullptr;
    const ip4_addr_t* ipr = nullptr;
    return etharp_find_addr(nif, &tgt, &eth, &ipr) >= 0 && eth != nullptr;
}

void Presence::update(uint32_t now) {
    if (!PRESENCE_HABILITADO || _target == 0) return;
    if (WiFi.status() != WL_CONNECTED) return;
    if (_lastProbe != 0 && (now - _lastProbe) < PRESENCE_PROBE_MS) return;
    _lastProbe = now;

    if (_probe()) _lastSeen = now;

    bool nowPresent = (_lastSeen != 0) && (now - _lastSeen) < PRESENCE_AWAY_MS;

    if (nowPresent && !_presente) {
        _presente = true;  _flagArrived = true;
        Serial.println("[pres] llegaste (teléfono presente)");
    } else if (!nowPresent && _presente) {
        _presente = false; _flagLeft = true;
        Serial.println("[pres] te fuiste (teléfono ausente)");
    }
}
