// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "fermentation.h"
#include "globals.h"
#include "hal_profiles.h"
#include "net_manager.h"

void ferm_start_preset(const char* style) {
    beerName = String("Físico: ") + String(style);

    if (strstr(style, "Lager")) {
        rampSteps[0] = {7, 10.0, "Primaria"};
        rampSteps[1] = {2, 16.0, "Diacetil"};
        rampSteps[2] = {14, 2.0, "Maturação"};
    } else {
        rampSteps[0] = {5, 18.0, "Primaria"};
        rampSteps[1] = {2, 22.0, "Diacetil"};
        rampSteps[2] = {7, 4.0, "Maturação"};
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
