// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef HAL_RELAYS_H
#define HAL_RELAYS_H

#include <Arduino.h>
#include "config.h"
#include "globals.h"

void init_relays();
void process_thermostat();
void override_relays(bool heaterState, bool coolerState);
void start_autotune();
// Polaridade (rele invertido = ativo em nivel baixo). Grava no NVS e reaplica na hora.
void relays_set_inverted(bool heater, bool cooler);
bool relay_heater_inverted();
bool relay_cooler_inverted();
extern bool isAutoTuning;
extern String autotuneMsg;

#endif // HAL_RELAYS_H
