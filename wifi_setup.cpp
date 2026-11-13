// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "wifi_setup.h"
#include "net_manager.h"
#include "platform.h"

static const unsigned long WIFI_SETUP_TIMEOUT_MS = 15000;
static WifiSetupState state = WIFI_SETUP_IDLE;
static unsigned long startedAt = 0;
static bool newNetwork = false;

void wifi_setup_connect_new(const String& ssid, const String& pass) {
    save_wifi_credentials(ssid, pass);
    attempt_wifi_connection(ssid, pass);
    newNetwork = true;
    startedAt = millis();
    state = WIFI_SETUP_CONNECTING;
}

void wifi_setup_track_saved() {
    newNetwork = false;
    startedAt = millis();
    state = WIFI_SETUP_CONNECTING;
}

WifiSetupState wifi_setup_poll() {
    if (state != WIFI_SETUP_CONNECTING) return state;
    if (WiFi.status() == WL_CONNECTED) {
        state = WIFI_SETUP_CONNECTED;
    } else if (millis() - startedAt > WIFI_SETUP_TIMEOUT_MS) {
        state = WIFI_SETUP_FAILED;
        if (newNetwork) {
            clear_wifi_credentials(); // senha errada ou rede fora de alcance
            start_wifi_scan();
        }
    }
    return state;
}

bool wifi_setup_connecting() { return state == WIFI_SETUP_CONNECTING && millis() - startedAt < WIFI_SETUP_TIMEOUT_MS + 5000; }
bool wifi_setup_is_new_network() { return newNetwork; }
void wifi_setup_reset() { state = WIFI_SETUP_IDLE; }
