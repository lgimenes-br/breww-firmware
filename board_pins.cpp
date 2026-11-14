// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "board_pins.h"
#include "config.h"
#include "platform.h"
#if defined(ESP8266)
#include <Wire.h>
#endif

#ifndef PIN_I2C_SDA
#define PIN_I2C_SDA -1
#define PIN_I2C_SCL -1
#endif

static const BoardPins DEFAULTS = {
    PIN_ONE_WIRE_BUS, PIN_DHT, PIN_RELAY_HEATER, PIN_RELAY_COOLER, PIN_I2C_SDA, PIN_I2C_SCL, -1, -1
};
BoardPins g_pins = DEFAULTS;

static String lastError;
static unsigned long lastErrorAt = 0;

bool pins_configurable() { return !BREWW_HAS_LVGL; }

// ---------------------------------------------------------------------------
// Pinos que nao podem ser usados, por chip
// ---------------------------------------------------------------------------
static bool pin_exists_and_free(int p, bool output, String& why) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (p < 0 || p > 48 || (p >= 22 && p <= 25)) { why = "nao existe no ESP32-S3"; return false; }
    if (p >= 26 && p <= 37) { why = "e da memoria flash/PSRAM"; return false; }
    if (p == 19 || p == 20) { why = "e do USB"; return false; }
    if (p == 43 || p == 44) { why = "e da serial (UART0)"; return false; }
    (void)output;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    if (p < 0 || p > 21 || (p > 10 && p < 18)) { why = (p > 10 && p < 18) ? "e da memoria flash" : "nao existe no ESP32-C3"; return false; }
    if (p == 18 || p == 19) { why = "e do USB"; return false; }
    (void)output;
#elif defined(ESP8266)
    if (p < 0 || p > 16) { why = "nao existe no ESP8266"; return false; }
    if (p >= 6 && p <= 11) { why = "e da memoria flash"; return false; }
    if (p == 1 || p == 3) { why = "e da serial (USB)"; return false; }
    if (p == 16 && !output) { why = "nao tem pull-up interno"; return false; }
    (void)output;
#else // ESP32 classico
    if (p < 0 || p > 39 || p == 20 || p == 24 || (p >= 28 && p <= 31)) { why = "nao existe no ESP32"; return false; }
    if (p >= 6 && p <= 11) { why = "e da memoria flash"; return false; }
    if (p == 1 || p == 3) { why = "e da serial (UART0)"; return false; }
    if (output && p >= 34) { why = "so funciona como entrada"; return false; }
    if (!output && p >= 34) { why = "nao tem pull-up interno"; return false; }
#endif
    if (p == PIN_BUTTON_BOOT) { why = "e o botao BOOT"; return false; }
    if (PIN_STATUS_LED >= 0 && p == PIN_STATUS_LED) { why = "e o LED da placa"; return false; }
    return true;
}

// Pinos de boot: um rele preso neles pode impedir a placa de ligar
static bool is_strapping(int p) {
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    return p == 0 || p == 3 || p == 45 || p == 46;
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return p == 2 || p == 8 || p == 9;
#elif defined(ESP8266)
    return p == 0 || p == 2 || p == 15 || p == 16; // 16 pulsa no reset (rele estalaria)
#else
    return p == 0 || p == 2 || p == 5 || p == 12 || p == 15;
#endif
}

bool pins_sensor_candidate(int p) {
    String why;
    if (!pin_exists_and_free(p, true, why) || is_strapping(p)) return false;
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    if (p == 48) return false; // LED RGB do DevKitC
#endif
    const BoardPins& g = g_pins;
    return p != g.heater && p != g.cooler && p != g.sda && p != g.scl && p != g.btnUp && p != g.btnDown;
}

bool pins_validate(const BoardPins& p, String& err) {
    struct Role { const char* name; int pin; bool required; bool output; bool relay; };
    const Role roles[] = {
        {"sensor de temperatura", p.oneWire, true, true, false},
        {"DHT22", p.dht, false, true, false},
        {"rele de aquecer", p.heater, true, true, true},
        {"rele de resfriar", p.cooler, true, true, true},
        {"I2C SDA", p.sda, false, true, false},
        {"I2C SCL", p.scl, false, true, false},
        {"botao subir", p.btnUp, false, false, false},
        {"botao descer", p.btnDown, false, false, false},
    };
    if ((p.sda < 0) != (p.scl < 0)) { err = "I2C precisa dos dois pinos (SDA e SCL) ou de nenhum"; return false; }
    for (size_t i = 0; i < sizeof(roles) / sizeof(roles[0]); i++) {
        const Role& r = roles[i];
        if (r.pin < 0) {
            if (r.required) { err = String(r.name) + ": obrigatorio"; return false; }
            continue;
        }
        String why;
        if (!pin_exists_and_free(r.pin, r.output, why)) { err = String(r.name) + ": GPIO " + r.pin + " " + why; return false; }
        if (r.relay && is_strapping(r.pin)) { err = String(r.name) + ": GPIO " + r.pin + " e pino de boot (a placa pode nao ligar)"; return false; }
        for (size_t j = 0; j < i; j++) {
            if (roles[j].pin == r.pin) { err = String(r.name) + ": GPIO " + r.pin + " ja usado em " + roles[j].name; return false; }
        }
    }
    return true;
}

#if defined(ESP8266)
// Placa ESP8266 com OLED integrado (ex.: HW-364A, "NodeMCU com tela 0,96"): o OLED fica no GPIO14/GPIO12,
// os mesmos pinos que a NodeMCU comum usa para os sensores. Sem pinos salvos, o boot procura um OLED ali
// (nas duas ordens, a serigrafia dessas placas varia); achou -> tela ligada e o DS18B20 vai para o D7
// (GPIO13). Nao achou -> pinos da NodeMCU comum. O mesmo firmware serve para as duas.
static bool probe_oled(int sda, int scl) {
    Wire.begin(sda, scl);
    for (uint8_t a : {(uint8_t)0x3C, (uint8_t)0x3D}) {
        Wire.beginTransmission(a);
        if (Wire.endTransmission() == 0) return true;
    }
    return false;
}
static bool detect_oled_board() {
    const int pairs[2][2] = {{14, 12}, {12, 14}};
    for (auto& pr : pairs) {
        if (probe_oled(pr[0], pr[1])) {
            g_pins.sda = pr[0];
            g_pins.scl = pr[1];
            g_pins.oneWire = 13; // D7
            g_pins.dht = -1;     // sem pino livre seguro sobrando (configuravel pelo app)
            Serial.printf_P(PSTR("[Pinos] Placa com OLED: tela no SDA %d / SCL %d; DS18B20 no D7 (GPIO13)\n"), pr[0], pr[1]);
            return true;
        }
    }
    pinMode(12, INPUT); // devolve os pinos aos sensores da NodeMCU comum
    pinMode(14, INPUT);
    return false;
}
#endif

void pins_load() {
    g_pins = DEFAULTS;
    if (!pins_configurable()) {
        Preferences p;
        p.begin("breww", true);
        if (p.getBool("sens_swap", false)) { g_pins.oneWire = DEFAULTS.dht; g_pins.dht = DEFAULTS.oneWire; }
        p.end();
        return;
    }
    Preferences p;
    p.begin("breww", true);
    BoardPins saved = DEFAULTS;
    bool has = p.isKey("pin_ow");
    if (has) {
        saved.oneWire = p.getChar("pin_ow", DEFAULTS.oneWire);
        saved.dht = p.getChar("pin_dht", DEFAULTS.dht);
        saved.heater = p.getChar("pin_h", DEFAULTS.heater);
        saved.cooler = p.getChar("pin_c", DEFAULTS.cooler);
        saved.sda = p.getChar("pin_sda", DEFAULTS.sda);
        saved.scl = p.getChar("pin_scl", DEFAULTS.scl);
    }
    saved.btnUp = p.getChar("pin_bup", -1);
    saved.btnDown = p.getChar("pin_bdn", -1);
    p.end();
#if defined(ESP8266)
    if (!has && detect_oled_board()) return; // pinos da placa com OLED
#endif
    String err;
    if (pins_validate(saved, err)) g_pins = saved;
    else Serial.printf_P(PSTR("[Pinos] Configuracao salva invalida (%s): usando os pinos padrao da placa\n"), err.c_str());
}

bool pins_save(const BoardPins& np, String& err) {
    if (!pins_configurable()) { err = "os pinos desta placa sao fixos"; return false; }
    if (!pins_validate(np, err)) return false;
    Preferences p;
    p.begin("breww", false);
    p.putChar("pin_ow", np.oneWire);
    p.putChar("pin_dht", np.dht);
    p.putChar("pin_h", np.heater);
    p.putChar("pin_c", np.cooler);
    p.putChar("pin_sda", np.sda);
    p.putChar("pin_scl", np.scl);
    p.putChar("pin_bup", np.btnUp);
    p.putChar("pin_bdn", np.btnDown);
    p.end();
    return true;
}

void pins_set_sensor_swap(bool swapped) {
    Preferences p;
    p.begin("breww", false);
    p.putBool("sens_swap", swapped);
    p.end();
}

void pins_reset_defaults() {
    Preferences p;
    p.begin("breww", false);
    for (const char* k : {"pin_ow", "pin_dht", "pin_h", "pin_c", "pin_sda", "pin_scl", "pin_bup", "pin_bdn", "sens_swap"}) p.remove(k);
    p.end();
}

void pins_to_json(char* out, size_t n) {
    snprintf_P(out, n, PSTR("{\"ow\":%d,\"dht\":%d,\"h\":%d,\"c\":%d,\"sda\":%d,\"scl\":%d,\"bup\":%d,\"bdn\":%d,\"cfg\":%s}"),
             g_pins.oneWire, g_pins.dht, g_pins.heater, g_pins.cooler, g_pins.sda, g_pins.scl, g_pins.btnUp, g_pins.btnDown,
             pins_configurable() ? "true" : "false");
}

const char* pins_last_error() {
    if (!lastError.length() || millis() - lastErrorAt > 60000) return nullptr;
    return lastError.c_str();
}
void pins_set_last_error(const String& err) { lastError = err; lastErrorAt = millis(); }
