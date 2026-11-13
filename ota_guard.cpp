// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "ota_guard.h"
#include "platform.h"

#if defined(ESP8266)
// ESP8266 (Lite): sem particao de volta. A imagem nova so e ativada se o MD5 conferir (cloud_ota_update).
void ota_guard_begin() {}
void ota_guard_loop(bool) {}
bool ota_guard_pending() { return false; }
bool ota_guard_rolled_back_once() { return false; }
#else
#include <esp_ota_ops.h>

// O nucleo Arduino confirma a versao nova sozinho no boot; retornando true, a confirmacao fica conosco
// (o bootloader do core 3.3.x tem CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
extern "C" bool verifyRollbackLater() { return true; }

// A volta do bootloader age no PRIMEIRO reinicio da versao em teste: um erro fatal isolado nos
// primeiros segundos (ja visto em 1 de cada ~6 atualizacoes, em versoes diferentes) fazia a
// atualizacao "falhar" mesmo com a versao boa. Agora a versao nova e confirmada para o bootloader
// logo no boot e o teste e nosso, gravado na NVS:
//  - 2 erros fatais/watchdog seguidos durante o teste -> volta para a versao anterior;
//  - 10 min sem conseguir ficar 30 s conectada a nuvem -> volta;
//  - 30 s conectada -> confirmada.
static const unsigned long CONFIRM_AFTER_MS = 30000;
static const unsigned long GIVE_UP_AFTER_MS = 10UL * 60000;
static const uint8_t MAX_CRASHES = 2;

static bool pending = false;
static bool rolledBackReport = false;
static unsigned long connectedSince = 0;

static bool crash_reset() {
    switch (esp_reset_reason()) {
        case ESP_RST_PANIC: case ESP_RST_INT_WDT: case ESP_RST_TASK_WDT: case ESP_RST_WDT: return true;
        default: return false;
    }
}

// A outra particao de app (a versao anterior), se tiver um firmware valido
static const esp_partition_t* previous_app() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* other = esp_ota_get_next_update_partition(running);
    esp_app_desc_t desc;
    if (!other || other == running || esp_ota_get_partition_description(other, &desc) != ESP_OK) return nullptr;
    return other;
}

static void roll_back(const char* why) {
    const esp_partition_t* prev = previous_app();
    Preferences p;
    p.begin("breww", false);
    p.remove("ota_trial");
    p.remove("ota_crash");
    if (prev) p.putBool("ota_rb_new", true); // a versao anterior avisa o servidor no boot
    p.end();
    pending = false;
    if (!prev) { Serial.printf_P(PSTR("[OTA] %s, mas nao ha versao anterior valida: mantendo esta\n"), why); return; }
    Serial.printf_P(PSTR("[OTA] %s: voltando para a versao anterior (%s)\n"), why, prev->label);
    Serial.flush();
    if (esp_ota_set_boot_partition(prev) == ESP_OK) { delay(200); ESP.restart(); }
    Serial.println(F("[OTA] Nao foi possivel trocar de particao: mantendo esta"));
}

void ota_guard_begin() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    const bool firstBoot = running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
    Preferences p;
    p.begin("breww", false);
    if (firstBoot) {
        // Primeiro boot da versao nova: o bootloader ja nao volta sozinho; o teste e nosso
        esp_ota_mark_app_valid_cancel_rollback();
        p.putBool("ota_trial", true);
        p.putUChar("ota_crash", 0);
        Serial.println(F("[OTA] Versao nova em teste: confirma depois de 30 s conectada a nuvem"));
    }
    pending = p.getBool("ota_trial", false);
    if (pending && !firstBoot && crash_reset()) {
        uint8_t n = p.getUChar("ota_crash", 0) + 1;
        p.putUChar("ota_crash", n);
        Serial.printf_P(PSTR("[OTA] Versao em teste reiniciou por erro fatal (%u de %u)\n"), n, MAX_CRASHES);
        if (n >= MAX_CRASHES) { p.end(); roll_back("Erro fatal repetido na versao nova"); p.begin("breww", false); }
    }
    // Voltou de uma versao que falhou? Avisa uma vez (pelo nosso teste ou pelo bootloader)
    const esp_partition_t* bad = esp_ota_get_last_invalid_partition();
    if (p.getBool("ota_rb_new", false)) {
        rolledBackReport = true;
        p.remove("ota_rb_new");
    } else if (bad) {
        if (!p.getBool("ota_rb_rep", false)) { rolledBackReport = true; p.putBool("ota_rb_rep", true); }
    } else if (p.isKey("ota_rb_rep")) {
        p.remove("ota_rb_rep");
    }
    p.end();
}

void ota_guard_loop(bool cloudConnected) {
    if (!pending) return;
    if (cloudConnected) {
        if (!connectedSince) connectedSince = millis();
        if (millis() - connectedSince >= CONFIRM_AFTER_MS) {
            Preferences p;
            p.begin("breww", false);
            p.remove("ota_trial");
            p.remove("ota_crash");
            p.end();
            pending = false;
            Serial.println(F("[OTA] Versao nova confirmada"));
            return;
        }
    } else {
        connectedSince = 0;
    }
    if (millis() >= GIVE_UP_AFTER_MS) roll_back("A versao nova nao conectou em 10 min");
}

bool ota_guard_pending() { return pending; }
bool ota_guard_rolled_back_once() { return rolledBackReport; }
#endif // !ESP8266
