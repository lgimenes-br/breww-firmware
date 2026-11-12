// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef HAL_PROFILES_H
#define HAL_PROFILES_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "globals.h"

extern bool request_dashboard_refresh; // ui.cpp: remonta a tela no proximo ciclo

void init_profiles();
void process_ramps();
void start_ramp_profile();
void save_ramp_steps();

/** Horas decorridas na etapa atual (pausa nao conta) */
float ramp_step_elapsed_hours();

// Comandos remotos (app/site/servidor)
bool ramp_set_profile(JsonArrayConst steps, int stepIndex, float stepTimeHours);
bool ramp_skip_step();
bool ramp_toggle_pause();
void ramp_stop();
void ramp_on_mode_change();

#endif
