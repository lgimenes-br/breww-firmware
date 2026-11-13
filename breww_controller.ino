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
#include "crash_report.h"

#include "platform.h"


void setup() {
    Serial.begin(115200);
    Serial.println(F("\n--- Breww Controller V2.0 ---"));
    ota_guard_begin(); // versao nova em teste? (atualizacao segura pelo ar)
    crash_report_check(); // erro fatal no boot anterior? (vai para o diagnostico remoto)
    
    pins_load();     // pinos da placa (padrao ou configurados no NVS) antes de sensores/reles
    settings_load(); // modo, alvo, PID e dados do chopp (device_settings.cpp)
    
    init_sensors();
    init_relays();

    init_ui();
    init_ispindel();
    init_network();
    init_profiles();

    Serial.printf_P(PSTR("[Memoria] RAM interna livre %u (maior bloco %u) | PSRAM livre %u\n"),
                  (unsigned)plat_free_heap(), (unsigned)plat_largest_block(), (unsigned)plat_psram_free());
}


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
        Serial.printf_P(PSTR("[LAG] loop %lu ms | rede %lu (reconexao MQTT %lu, mqtt.loop %lu, telemetria %lu, cloud %lu, ota %lu, web %lu) | sensores %lu | controle %lu | tela %lu | RSSI %d\n"),
                      total, tNet, prof_reconnect, prof_mqttloop, prof_telemetry, prof_cloud, prof_ota, prof_web,
                      tSens, tCtrl, tUi, (int)WiFi.RSSI());
        // Envia tambem para o servidor (consulta remota: workflow "Diagnosticos das placas")
        static unsigned long lastLagReport = 0;
        if (millis() - lastLagReport > 2000) { // no maximo 1 evento a cada 2 s
            lastLagReport = millis();
            char ev[240];
            snprintf_P(ev, sizeof(ev), PSTR("{\"type\":\"lag\",\"ms\":%lu,\"net\":%lu,\"reconnect\":%lu,\"mqttloop\":%lu,\"telemetry\":%lu,\"cloud\":%lu,\"ota\":%lu,\"web\":%lu,\"sens\":%lu,\"ctrl\":%lu,\"ui\":%lu,\"rssi\":%d,\"up\":%lu}"),
                     total, tNet, prof_reconnect, prof_mqttloop, prof_telemetry, prof_cloud, prof_ota, prof_web,
                     tSens, tCtrl, tUi, (int)WiFi.RSSI(), millis() / 1000);
            diag_report(ev);
        }
    }
}
