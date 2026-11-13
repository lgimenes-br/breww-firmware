// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "fermentation.h"
#include "globals.h"
#include "hal_profiles.h"
#include "net_manager.h"

static void set_step(int i, float days, float temp, const char* name) {
    rampSteps[i].durationDays = days;
    rampSteps[i].targetTemp = temp;
    strncpy(rampSteps[i].name, name, sizeof(rampSteps[i].name) - 1);
    rampSteps[i].name[sizeof(rampSteps[i].name) - 1] = '\0';
}

void ferm_start_preset(const char* style) {
    beerName = String("Físico: ") + String(style);

    if (strstr(style, "Lager")) {
        set_step(0, 7, 10.0, "Primaria");
        set_step(1, 2, 16.0, "Diacetil");
        set_step(2, 14, 2.0, "Maturação");
    } else {
        set_step(0, 5, 18.0, "Primaria");
        set_step(1, 2, 22.0, "Diacetil");
        set_step(2, 7, 4.0, "Maturação");
    }
    numRampSteps = 3;
    start_ramp_profile();

    String payload = "{\"action\":\"start_batch\", \"name\":\"" + beerName + "\", \"style\":\"" + String(style) + "\"}";
    mqtt_publish_device_action(payload.c_str());
}

void ferm_stop_batch() {
    ramp_stop();
    mqtt_publish_device_action("{\"action\":\"stop_batch\"}");
}
