// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include <Arduino.h>
#include "config.h"
#include "globals.h"
#include "ispindel.h"
#include "ui.h"
#include "hal_sensors.h"
#include "hal_relays.h"
#include "net_manager.h"
#include "hal_profiles.h"
#include "device_settings.h"
#include "board_pins.h"
#include "ota_guard.h"

#include <Preferences.h>
#include <esp_heap_caps.h>


void setup() {
    Serial.begin(115200);
    Serial.println("\n--- Breww Controller V2.0 ---");
    ota_guard_begin(); // versao nova em teste? (atualizacao segura pelo ar)
    
    pins_load();     // pinos da placa (padrao ou configurados no NVS) antes de sensores/reles
    settings_load(); // modo, alvo, PID e dados do chopp (device_settings.cpp)
    
    init_sensors();
    init_relays();

    init_ui();
    init_ispindel();
    init_network();
    init_profiles();

    Serial.printf("[Memoria] RAM interna livre %u (maior bloco %u) | PSRAM livre %u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

#include <esp_task_wdt.h>

// Detector de travamento: se uma volta do loop passar de 300 ms, mostra qual parte demorou.
// (Comando MQTT que chega durante um travamento so e processado quando o loop volta.)
extern unsigned long prof_ota, prof_cloud, prof_reconnect, prof_mqttloop, prof_telemetry, prof_web;

void loop() {
    unsigned long t0 = millis();
    handle_network();
    ota_guard_loop(mqttClient.connected());
    settings_loop(); // grava alteracoes pendentes (debounce)
    unsigned long tNet = millis() - t0;
    unsigned long tSens = 0, tCtrl = 0, tUi = 0;

    if (!isUpdatingOTA) {
        unsigned long t = millis();
        read_temperatures();
        tSens = millis() - t; t = millis();
        process_thermostat();
        process_ramps();
        tCtrl = millis() - t; t = millis();
        ui_update_temperatures();
        loop_ui();
        tUi = millis() - t;
    }

    unsigned long total = millis() - t0;
    if (total > 300 && !isUpdatingOTA) {
        Serial.printf("[LAG] loop %lu ms | rede %lu (reconexao MQTT %lu, mqtt.loop %lu, telemetria %lu, cloud %lu, ota %lu, web %lu) | sensores %lu | controle %lu | tela %lu | RSSI %d\n",
                      total, tNet, prof_reconnect, prof_mqttloop, prof_telemetry, prof_cloud, prof_ota, prof_web,
                      tSens, tCtrl, tUi, (int)WiFi.RSSI());
        // Envia tambem para o servidor (consulta remota: workflow "Diagnosticos das placas")
        static unsigned long lastLagReport = 0;
        if (millis() - lastLagReport > 2000) { // no maximo 1 evento a cada 2 s
            lastLagReport = millis();
            char ev[240];
            snprintf(ev, sizeof(ev),
                     "{\"type\":\"lag\",\"ms\":%lu,\"net\":%lu,\"reconnect\":%lu,\"mqttloop\":%lu,\"telemetry\":%lu,\"cloud\":%lu,\"ota\":%lu,\"web\":%lu,\"sens\":%lu,\"ctrl\":%lu,\"ui\":%lu,\"rssi\":%d,\"up\":%lu}",
                     total, tNet, prof_reconnect, prof_mqttloop, prof_telemetry, prof_cloud, prof_ota, prof_web,
                     tSens, tCtrl, tUi, (int)WiFi.RSSI(), millis() / 1000);
            diag_report(ev);
        }
    }
}
