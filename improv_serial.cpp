// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "config.h"
#if !BREWW_HAS_LVGL

#include "improv_serial.h"
#include "wifi_setup.h"
#include "wifi_portal.h"
#include "version.h"
#include "platform.h"

// ---- Protocolo ----
enum : uint8_t { TYPE_STATE = 0x01, TYPE_ERROR = 0x02, TYPE_RPC = 0x03, TYPE_RPC_RESULT = 0x04 };
enum : uint8_t { STATE_READY = 0x02, STATE_PROVISIONING = 0x03, STATE_PROVISIONED = 0x04 };
enum : uint8_t { ERR_NONE = 0x00, ERR_INVALID_RPC = 0x01, ERR_UNKNOWN_RPC = 0x02, ERR_UNABLE_TO_CONNECT = 0x03 };
enum : uint8_t { CMD_WIFI_SETTINGS = 0x01, CMD_GET_STATE = 0x02, CMD_GET_INFO = 0x03, CMD_GET_NETWORKS = 0x04 };

static const char HEADER[] = "IMPROV";
static const unsigned long CONNECT_TIMEOUT_MS = 28000; // mesh: o 1o no pode recusar e o driver tenta outro (o site espera 30 s)
static const char* NEXT_URL = "https://breww.live/instalar#app"; // botao "Visitar" do instalador

static uint8_t buf[256];
static size_t len = 0;
static bool provisioning = false;
static unsigned long provisioningSince = 0;

static const char* chip_family() { return plat_chip_family(); }

static void send_packet(uint8_t type, const uint8_t* data, uint8_t n) {
    uint8_t out[6 + 3 + 255 + 1];
    size_t i = 0;
    for (int k = 0; k < 6; k++) out[i++] = (uint8_t)HEADER[k];
    out[i++] = 0x01;          // versao
    out[i++] = type;
    out[i++] = n;
    memcpy(out + i, data, n); i += n;
    uint8_t sum = 0;
    for (size_t k = 0; k < i; k++) sum += out[k];
    out[i++] = sum;
    Serial.write(out, i);
    Serial.write('\n');
    Serial.flush();
}

static String bytes_to_string(const uint8_t* p, size_t n) { String s; s.reserve(n); for (size_t i = 0; i < n; i++) s += (char)p[i]; return s; }

static void send_state(uint8_t s) { send_packet(TYPE_STATE, &s, 1); }
static void send_error(uint8_t e) { send_packet(TYPE_ERROR, &e, 1); }

/** Resultado de RPC: comando | tamanho | (tamanho da string, string)... */
static void send_result(uint8_t cmd, const char* const* strs, size_t count) {
    uint8_t data[250];
    size_t i = 2;
    for (size_t k = 0; k < count; k++) {
        size_t sl = strlen(strs[k]);
        if (i + 1 + sl > sizeof(data)) break;
        data[i++] = (uint8_t)sl;
        memcpy(data + i, strs[k], sl); i += sl;
    }
    data[0] = cmd;
    data[1] = (uint8_t)(i - 2);
    send_packet(TYPE_RPC_RESULT, data, (uint8_t)i);
}

static void handle_rpc(const uint8_t* d, uint8_t n) {
    if (n < 2 || d[1] != n - 2) { send_error(ERR_INVALID_RPC); return; }
    const uint8_t cmd = d[0];
    const uint8_t* p = d + 2;
    const uint8_t plen = d[1];
    send_error(ERR_NONE);

    if (cmd == CMD_GET_STATE) {
        if (WiFi.status() == WL_CONNECTED) {
            send_state(STATE_PROVISIONED);
            const char* s[] = {NEXT_URL};
            send_result(CMD_GET_STATE, s, 1);
        } else {
            send_state(provisioning ? STATE_PROVISIONING : STATE_READY);
        }
    } else if (cmd == CMD_GET_INFO) {
        String name = wifi_portal_ssid(); // "Breww-1A2B"
        const char* s[] = {"Breww", FW_VERSION, chip_family(), name.c_str()};
        send_result(CMD_GET_INFO, s, 4);
    } else if (cmd == CMD_GET_NETWORKS) {
        // Sincrono (~2 s), so quando o instalador pede. Logo depois de gravar, o portal Wi-Fi abre e dispara
        // uma busca em segundo plano: a busca daqui voltava -1/0 e o site mostrava "nenhuma rede". Espera a
        // busca em andamento terminar e tenta de novo uma vez.
        int found = WiFi.scanNetworks(false, false);
        if (found <= 0) {
            unsigned long t0 = millis();
            while (WiFi.scanComplete() == WIFI_SCAN_RUNNING && millis() - t0 < 8000) delay(50);
            WiFi.scanDelete();
            delay(200);
            found = WiFi.scanNetworks(false, false);
        }
        for (int k = 0; k < found && k < 20; k++) {
            String ssid = WiFi.SSID(k);
            if (!ssid.length()) continue;
            String rssi = String(WiFi.RSSI(k));
            const char* s[] = {ssid.c_str(), rssi.c_str(), WiFi.encryptionType(k) == WIFI_AUTH_OPEN ? "NO" : "YES"};
            send_result(CMD_GET_NETWORKS, s, 3);
        }
        WiFi.scanDelete();
        send_result(CMD_GET_NETWORKS, nullptr, 0); // fim da lista
    } else if (cmd == CMD_WIFI_SETTINGS) {
        if (plen < 1) { send_error(ERR_INVALID_RPC); return; }
        uint8_t sl = p[0];
        if (1 + sl + 1 > plen) { send_error(ERR_INVALID_RPC); return; }
        String ssid = bytes_to_string(p + 1, sl);
        uint8_t pl = p[1 + sl];
        if (1 + sl + 1 + pl > plen) { send_error(ERR_INVALID_RPC); return; }
        String pass = bytes_to_string(p + 2 + sl, pl);
        send_state(STATE_PROVISIONING);
        provisioning = true;
        provisioningSince = millis();
        wifi_setup_connect_new(ssid, pass); // grava, conecta (rede nova que falha tem a senha apagada)
    } else {
        send_error(ERR_UNKNOWN_RPC);
    }
}

bool improv_feed(uint8_t b) {
    if (len < 6) {
        if (b == (uint8_t)HEADER[len]) { buf[len++] = b; return true; }
        len = 0;
        if (b == (uint8_t)HEADER[0]) { buf[len++] = b; return true; }
        return false; // texto normal (comandos do serial)
    }
    buf[len++] = b;
    if (len < 9) return true;                      // versao, tipo, tamanho
    const size_t total = 9 + buf[8] + 1;            // + dados + checksum
    if (len < total) {
        if (len >= sizeof(buf)) len = 0;            // pacote invalido grande demais
        return true;
    }
    uint8_t sum = 0;
    for (size_t k = 0; k < total - 1; k++) sum += buf[k];
    const bool ok = sum == buf[total - 1] && buf[6] == 0x01;
    const uint8_t type = buf[7];
    if (ok && type == TYPE_RPC) handle_rpc(buf + 9, buf[8]);
    else if (!ok) send_error(ERR_INVALID_RPC);
    len = 0;
    return true;
}

void improv_loop() {
    if (!provisioning) return;
    if (WiFi.status() == WL_CONNECTED) {
        provisioning = false;
        send_state(STATE_PROVISIONED);
        const char* s[] = {NEXT_URL};
        send_result(CMD_WIFI_SETTINGS, s, 1);
    } else if (millis() - provisioningSince > CONNECT_TIMEOUT_MS) {
        provisioning = false;
        send_error(ERR_UNABLE_TO_CONNECT);
        send_state(STATE_READY);
    }
}

#endif // !BREWW_HAS_LVGL
