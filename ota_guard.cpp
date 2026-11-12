// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "ota_guard.h"
#include <esp_ota_ops.h>
#include <Preferences.h>

// O nucleo Arduino confirma a versao nova sozinho no boot; retornando true, a confirmacao fica com
// o ota_guard_loop (o bootloader do core 3.3.x tem CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE).
extern "C" bool verifyRollbackLater() { return true; }

static const unsigned long CONFIRM_AFTER_MS = 30000;        // conectado e estavel por 30 s
static const unsigned long GIVE_UP_AFTER_MS = 10UL * 60000; // 10 min sem conseguir: volta

static bool pending = false;
static bool rolledBackReport = false;
static unsigned long connectedSince = 0;

void ota_guard_begin() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t st;
    if (running && esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY) {
        pending = true;
        Serial.println("[OTA] Versao nova em teste: confirma depois de 30 s conectada a nuvem (ou volta em 10 min)");
    }
    // Voltou de uma versao que falhou? Avisa uma vez (a particao invalida continua marcada ate a proxima OTA nela)
    const esp_partition_t* bad = esp_ota_get_last_invalid_partition();
    Preferences p;
    p.begin("breww", false);
    if (bad) {
        if (!p.getBool("ota_rb_rep", false)) { rolledBackReport = true; p.putBool("ota_rb_rep", true); }
        Serial.printf("[OTA] A versao da particao %s falhou e foi descartada\n", bad->label);
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
            esp_ota_mark_app_valid_cancel_rollback();
            pending = false;
            Serial.println("[OTA] Versao nova confirmada");
            return;
        }
    } else {
        connectedSince = 0;
    }
    if (millis() >= GIVE_UP_AFTER_MS) {
        Serial.println("[OTA] A versao nova nao conectou em 10 min: voltando para a anterior");
        Serial.flush();
        delay(200);
        esp_ota_mark_app_invalid_rollback_and_reboot(); // so retorna se nao houver versao anterior valida
        Serial.println("[OTA] Sem versao anterior para voltar: mantendo esta");
        esp_ota_mark_app_valid_cancel_rollback();
        pending = false;
    }
}

bool ota_guard_pending() { return pending; }
bool ota_guard_rolled_back_once() { return rolledBackReport; }
