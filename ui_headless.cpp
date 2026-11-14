// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// Interface para placas SEM tela grande (build "modular": esp32-devkit, s3-headless, c3-headless).
// Implementa as mesmas funcoes de ui.h com:
//  - LED de status (padroes de pisca abaixo);
//  - botao BOOT: segurar 3 s confirma um pedido de vinculo; segurar 10 s apaga o Wi-Fi e reinicia;
//  - monitor serial (115200): comandos "wifi", "codigo", "status", "reiniciar".
//  - tela pequena I2C opcional (OLED 128x64 ou LCD 20x4), detectada no boot (small_display.cpp).
#include "config.h"
#if !BREWW_HAS_LVGL

#include "ui.h"
#include "globals.h"
#include "net_manager.h"
#include "cloud_pairing.h"
#include "wifi_setup.h"
#include "small_display.h"
#include "hal_relays.h"
#include "wifi_portal.h"
#include "board_pins.h"
#include "i2c_sensors.h"
#include "button_menu.h"
#include "improv_serial.h"
#include "hal_sensors.h"
#include "platform.h"

// ---------------- Estado mostrado no LED ----------------
static bool otaActive = false;
static int otaPercent = 0;
static bool pairPending = false;
static unsigned long pairDeadline = 0;
static String pairEmail;

// Padroes do LED (periodo, tempo aceso):
//  atualizando ........ pisca rapido (100/100 ms)
//  pedido de vinculo .. duas piscadas por segundo
//  sem Wi-Fi .......... pisca lento (1 s / 1 s)
//  Wi-Fi sem nuvem .... piscada curta a cada 2 s
//  tudo conectado ..... aceso
static void led_write(bool on) {
    if (PIN_STATUS_LED < 0) return;
    digitalWrite(PIN_STATUS_LED, (on != (bool)STATUS_LED_ACTIVE_LOW) ? HIGH : LOW);
}

static void led_update() {
    if (PIN_STATUS_LED < 0) return;
    unsigned long t = millis();
    bool on;
    if (otaActive) on = (t / 100) % 2 == 0;
    else if (pairPending) { unsigned long p = t % 1000; on = p < 120 || (p >= 240 && p < 360); }
    else if (WiFi.status() != WL_CONNECTED) on = (t / 1000) % 2 == 0;
    else if (!mqttClient.connected()) on = (t % 2000) < 100;
    else on = true;
    led_write(on);
}

// ---------------- Botao BOOT ----------------
static const unsigned long HOLD_CONFIRM_MS = 3000;
static const unsigned long HOLD_RESET_WIFI_MS = 10000;
static unsigned long pressedAt = 0;
static bool holdHandled = false;

static void button_update() {
    bool pressed = digitalRead(PIN_BUTTON_BOOT) == LOW;
    unsigned long t = millis();
    if (pressed && pressedAt == 0) { pressedAt = t; holdHandled = false; }
    if (!pressed) { pressedAt = 0; return; }
    if (holdHandled) return;
    unsigned long held = t - pressedAt;
    if (pairPending && held >= HOLD_CONFIRM_MS) {
        holdHandled = true;
        Serial.println(F("[Botao] Vinculo CONFIRMADO pelo botao"));
        cloud_pairing_respond(true);
        pairPending = false;
    } else if (!pairPending && held >= HOLD_RESET_WIFI_MS) {
        holdHandled = true;
        Serial.println(F("[Botao] 10 s: apagando o Wi-Fi salvo e reiniciando"));
        clear_wifi_credentials();
        delay(300);
        ESP.restart();
    }
}

// ---------------- Serial ----------------
static void print_help() {
    Serial.println(F("Comandos: wifi <rede> <senha> | codigo | status | redes | tela sh1106|ssd1306 | rele normal|invertido | pinos | pino <papel> <gpio> | reiniciar"));
}

static void print_status() {
    String code;
    Serial.printf_P(PSTR("[Status] placa %s | modo %d | temp %.2f / alvo %.2f | Wi-Fi %s (%s) | nuvem %s\n"),
                  BREWW_BOARD_NAME, currentMode, currentTemp, currentSetpoint,
                  WiFi.status() == WL_CONNECTED ? "conectado" : "desconectado",
                  WiFi.SSID().c_str(), mqttClient.connected() ? "conectada" : "desconectada");
    if (cloud_pair_code(code)) Serial.printf_P(PSTR("[Status] codigo de confirmacao: %s (ID %s)\n"), code.c_str(), get_device_id().c_str());
    // Diagnostico da instalacao (o assistente do site guarda estas linhas no log)
    if (WiFi.status() == WL_CONNECTED)
        Serial.printf_P(PSTR("[Status] Wi-Fi: no %s, canal %d, %d dBm | rede salva: %s | nos que recusaram: %d\n"), WiFi.BSSIDstr().c_str(), (int)WiFi.channel(), (int)WiFi.RSSI(), has_wifi_credentials() ? "sim" : "NAO (nao reconecta apos reiniciar)", wifi_rejected_count());
    Serial.printf_P(PSTR("[Status] sensor %s | chip %.0f C | RAM livre %u | ligado ha %lu s | nuvem %s\n"),
                  control_sensor_ok() ? "ok" : "sem leitura (reles desligados)",
                  plat_chip_temp(), (unsigned)ESP.getFreeHeap(), millis() / 1000, cloud_status_text());
}

static void serial_update() {
    static String line;
    while (Serial.available()) {
        char c = (char)Serial.read();
        if (improv_feed((uint8_t)c)) continue; // pacote do instalador do site (Wi-Fi pelo USB)
        if (c == '\r') continue;
        if (c != '\n') { if (line.length() < 160) line += c; continue; }
        line.trim();
        if (line.startsWith("wifi ")) {
            // wifi <rede> <senha>  (rede sem espacos; para rede com espaco use aspas: wifi "Minha Rede" senha)
            String rest = line.substring(5); rest.trim();
            String ssid, pass;
            if (rest.startsWith("\"")) {
                int q = rest.indexOf('"', 1);
                ssid = q > 0 ? rest.substring(1, q) : rest.substring(1);
                pass = q > 0 ? rest.substring(q + 1) : "";
            } else {
                int sp = rest.indexOf(' ');
                ssid = sp > 0 ? rest.substring(0, sp) : rest;
                pass = sp > 0 ? rest.substring(sp + 1) : "";
            }
            pass.trim();
            Serial.printf_P(PSTR("[Wi-Fi] Conectando em \"%s\"...\n"), ssid.c_str());
            wifi_setup_connect_new(ssid, pass);
        } else if (line == "codigo" || line == "code") {
            String code;
            if (cloud_pair_code(code)) Serial.printf_P(PSTR("[Vinculo] Codigo de confirmacao: %s (ID %s)\n"), code.c_str(), get_device_id().c_str());
            else Serial.println(F("[Vinculo] Codigo ainda nao gerado (conecte o Wi-Fi primeiro)"));
        } else if (line == "status") {
            print_status();
        } else if (line == "redes") {
            // Nos da rede (mesh): BSSID, canal e sinal de cada um; * = o no em uso
            Serial.println(F("[Redes] Procurando (todos os canais)..."));
            int n = WiFi.scanNetworks(false, true);
            String cur = WiFi.status() == WL_CONNECTED ? WiFi.BSSIDstr() : String("");
            for (int i = 0; i < n; i++) {
                Serial.printf_P(PSTR("[Redes] %c %-24s %s  canal %2d  %4d dBm\n"), WiFi.BSSIDstr(i) == cur ? '*' : ' ',
                                WiFi.SSID(i).c_str(), WiFi.BSSIDstr(i).c_str(), (int)WiFi.channel(i), (int)WiFi.RSSI(i));
            }
            if (n <= 0) Serial.println(F("[Redes] Nenhuma rede encontrada"));
            WiFi.scanDelete();
        } else if (line == "pinos") {
            char buf[160]; pins_to_json(buf, sizeof(buf));
            Serial.printf_P(PSTR("[Pinos] %s\n[Pinos] Papeis: ow dht h c sda scl bup bdn (-1 = sem). Ex.: pino h 25 | pinos padrao\n"), buf);
        } else if (line == "pinos padrao") {
            pins_reset_defaults(); Serial.println(F("[Pinos] Padrao da placa. Reiniciando...")); delay(300); ESP.restart();
        } else if (line.startsWith("pino ")) {
            // pino <papel> <gpio>   papeis: ow dht h c sda scl bup bdn
            String rest = line.substring(5); rest.trim();
            int sp = rest.indexOf(' ');
            String role = sp > 0 ? rest.substring(0, sp) : rest;
            int gpio = sp > 0 ? rest.substring(sp + 1).toInt() : -2;
            BoardPins np = g_pins;
            int8_t* slot = role == "ow" ? &np.oneWire : role == "dht" ? &np.dht : role == "h" ? &np.heater : role == "c" ? &np.cooler
                         : role == "sda" ? &np.sda : role == "scl" ? &np.scl : role == "bup" ? &np.btnUp : role == "bdn" ? &np.btnDown : nullptr;
            String err;
            if (!slot || gpio < -1) Serial.println(F("[Pinos] Use: pino <ow|dht|h|c|sda|scl|bup|bdn> <gpio ou -1>"));
            else { *slot = (int8_t)gpio; if (pins_save(np, err)) { Serial.println(F("[Pinos] Gravado. Reiniciando...")); delay(300); ESP.restart(); } else Serial.printf_P(PSTR("[Pinos] Recusado: %s\n"), err.c_str()); }
        } else if (line.startsWith("rele")) {
            // rele normal | rele invertido | rele aquecer invertido | rele resfriar normal
            String arg = line.substring(4); arg.trim(); arg.toLowerCase();
            bool inv = arg.endsWith("invertido");
            bool ok = inv || arg.endsWith("normal");
            if (!ok) Serial.println(F("[Reles] Use: rele normal | rele invertido | rele aquecer invertido | rele resfriar normal"));
            else if (arg.startsWith("aquecer")) relays_set_inverted(inv, relay_cooler_inverted());
            else if (arg.startsWith("resfriar")) relays_set_inverted(relay_heater_inverted(), inv);
            else relays_set_inverted(inv, inv);
        } else if (line.startsWith("tela ")) {
            String drv = line.substring(5); drv.trim(); drv.toLowerCase();
            if (small_display_set_oled_driver(drv)) { Serial.printf_P(PSTR("[Tela] Driver do OLED: %s. Reiniciando...\n"), drv.c_str()); delay(300); ESP.restart(); }
            else Serial.println(F("[Tela] Use: tela sh1106  (OLED 1,3\")  ou  tela ssd1306  (OLED 0,96\")"));
        } else if (line == "reiniciar" || line == "reboot") {
            ESP.restart();
        } else if (line.length()) {
            print_help();
        }
        line = "";
    }
}

// ---------------- ui.h ----------------
void init_ui() {
    if (PIN_STATUS_LED >= 0) pinMode(PIN_STATUS_LED, OUTPUT);
    led_write(false);
    pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);
    Serial.printf_P(PSTR("[UI] Placa %s sem tela grande. "), BREWW_BOARD_NAME);
    print_help();
    if (!has_wifi_credentials()) Serial.println(F("[UI] Sem Wi-Fi salvo: conecte o celular na rede Breww-XXXX (portal) ou use  wifi <rede> <senha>"));
    small_display_begin();
    i2c_sensors_begin(); // mesmo barramento da tela pequena
    menu_begin();        // botoes subir/descer (se configurados)
}

// ---------------- Tela pequena ----------------
static const char* mode_name(int m) {
    switch (m) {
        case MODE_FERM: return "Fermentador";
        case MODE_GELADEIRA: return "Geladeira";
        case MODE_CHOPP: return "Choppeira";
        case MODE_CLEAN: return "Limpeza";
        default: return "Breww";
    }
}

static void display_update() {
    if (!small_display_kind()) return;
    // Codigo de vinculo enquanto o aparelho nao tem credencial (ainda nao vinculado); checado a cada 5 s
    static String pairCode;
    static unsigned long lastCodeCheck = 0;
    if (millis() - lastCodeCheck > 5000 || lastCodeCheck == 0) {
        lastCodeCheck = millis();
        String u, p, code;
        if (!cloud_mqtt_credentials(u, p) && cloud_pair_code(code)) pairCode = code;
        else pairCode = "";
    }
    static char footer[40];
    if (currentMode == MODE_FERM && isRampActive && numRampSteps > 0)
        snprintf_P(footer, sizeof(footer), PSTR("Etapa %d/%d %s"), currentRampStep + 1, numRampSteps, rampSteps[currentRampStep].name);
    else if (currentMode == MODE_CHOPP)
        snprintf_P(footer, sizeof(footer), PSTR("%s"), beerName.c_str());
    else if (wifi_portal_active() && WiFi.status() != WL_CONNECTED)
        snprintf_P(footer, sizeof(footer), PSTR("Wi-Fi: %s"), wifi_portal_ssid().c_str());
    else if (WiFi.status() != WL_CONNECTED)
        snprintf_P(footer, sizeof(footer), PSTR("Sem Wi-Fi"));
    else footer[0] = 0;

    SmallDisplayState st;
    st.modeName = mode_name(currentMode);
    st.temp = control_temp(); // mesma sonda do controle
    st.setpoint = currentSetpoint;
    st.relay = isHeaterOn ? "Aquecendo" : (isCoolerOn ? "Resfriando" : "Desligado");
    st.wifi = WiFi.status() == WL_CONNECTED;
    st.cloud = mqttClient.connected();
    st.footer = footer;
    st.pairCode = pairCode.length() ? pairCode.c_str() : nullptr;
    st.pairPending = pairPending;
    st.pairEmail = pairEmail.c_str();
    st.otaActive = otaActive;
    st.otaPercent = otaPercent;
    if (menu_active()) menu_fill(st);
    small_display_update(st);
}

void loop_ui() {
    // Acompanha a conexao iniciada pelo comando "wifi" (rede nova que falha tem a senha apagada)
    WifiSetupState ws = wifi_setup_poll();
    if (ws == WIFI_SETUP_CONNECTED) { Serial.printf_P(PSTR("[Wi-Fi] Conectado (%s)\n"), WiFi.localIP().toString().c_str()); wifi_setup_reset(); }
    else if (ws == WIFI_SETUP_FAILED) { Serial.println(F("[Wi-Fi] Nao conectou em 15 s (senha errada?)")); wifi_setup_reset(); }

    if (pairPending && (long)(millis() - pairDeadline) > 0) {
        pairPending = false;
        Serial.println(F("[Vinculo] Pedido expirou"));
    }
    wifi_portal_loop(); // abre a rede "Breww-XXXX" sem Wi-Fi configurado (ou sem conectar ha 3 min)
    button_update();
    serial_update();
    improv_loop();
    menu_loop();
    led_update();
    display_update();
    delay(5);
}

void ui_update_temperatures() {
    request_dashboard_refresh = false; // nada para remontar sem tela
    static unsigned long lastLog = 0;
    if (millis() - lastLog > 60000) { lastLog = millis(); print_status(); }
}
void ui_update_relays() {}
void ui_update_ispindel() {}

void ui_show_ota_screen() { otaActive = true; otaPercent = 0; Serial.println(F("[OTA] Atualizando firmware...")); }
void ui_update_ota_progress(int percent) {
    if (percent / 10 != otaPercent / 10) Serial.printf_P(PSTR("[OTA] %d%%\n"), percent);
    otaPercent = percent;
}
void ui_ota_status(const char* text) { Serial.printf_P(PSTR("[OTA] %s\n"), text); }
void ui_hide_ota_screen() { otaActive = false; }
void ui_show_ota_success(bool success) {
    Serial.println(success ? "[OTA] Concluida, reiniciando" : "[OTA] Falhou");
    if (!success) otaActive = false;
}

void ui_show_pair_confirm(const char* email, const char* deviceName, int expiresIn) {
    pairPending = true;
    pairDeadline = millis() + (unsigned long)(expiresIn > 0 ? expiresIn : 120) * 1000UL;
    pairEmail = email ? email : "";
    Serial.printf_P(PSTR("[Vinculo] Pedido de %s para \"%s\". Segure o botao BOOT por 3 s para confirmar.\n"),
                  email ? email : "?", deviceName ? deviceName : "");
}
void ui_hide_pair_confirm() { pairPending = false; }
void ui_pair_result(const char* status, bool requestOk) {
    pairPending = false;
    Serial.printf_P(PSTR("[Vinculo] Resultado: %s%s\n"), status && *status ? status : "?", requestOk ? "" : " (falha de rede)");
}

#endif // !BREWW_HAS_LVGL
