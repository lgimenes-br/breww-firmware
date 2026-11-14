// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "device_settings.h"
#include "globals.h"
#include "hal_profiles.h"   // ramp_on_mode_change, request_dashboard_refresh
#include "net_manager.h"    // mqtt_publish_telemetry
#include "platform.h"
#include <AutoPID.h>

extern AutoPID myPID;      // hal_relays.cpp

static const unsigned long SAVE_DEBOUNCE_MS = 500;
static unsigned long pendingSaveAt = 0;   // 0 = nada pendente
static bool pendingBeerChange = false;    // dados do chopp mudaram: remonta a tela ao gravar

static void schedule_save() {
    pendingSaveAt = millis() + SAVE_DEBOUNCE_MS;
    if (pendingSaveAt == 0) pendingSaveAt = 1;
}

void settings_load() {
    Preferences p;
    p.begin("breww", false); // RW para permitir migracao
    currentSetpoint = p.getFloat("setpoint", 18.0);
    finalGravity = p.getFloat("fg", 1.010);
    pidKp = p.getDouble("kp", 200.0);
    pidKi = p.getDouble("ki", 1.0);
    pidKd = p.getDouble("kd", 0.0);
    pwmWindow = p.getInt("pwm_win", 30000); // 30 s para reles mecanicos

    // Migracao para quem tinha o padrao antigo de 2 s salvo (protege reles mecanicos)
    if (pwmWindow == 2000) {
        pwmWindow = 30000;
        p.putInt("pwm_win", 30000);
    }

    currentMode = p.getInt("mode", -1);
    if (currentMode == -1) {
        // Migracao da chave antiga; placa nova (gravada do zero) comeca no modo geladeira (decisao do dono)
        currentMode = p.getInt("currentMode", MODE_GELADEIRA);
        p.putInt("mode", currentMode);
    }

    beerName = p.getString("beerName", "MINHA BREJA");
    beerType = p.getString("beerType", "IPA");
    beerIBU = p.getInt("beerIBU", 40);
    beerABV = p.getFloat("beerABV", 5.5);
    p.end();
}

void settings_loop() {
    if (pendingSaveAt == 0 || (long)(millis() - pendingSaveAt) < 0) return;
    pendingSaveAt = 0;

    Preferences p;
    p.begin("breww", false);
    p.putString("beerName", beerName);
    p.putString("beerType", beerType);
    p.putInt("beerIBU", beerIBU);
    p.putFloat("beerABV", beerABV);
    p.putInt("mode", currentMode);
    p.putFloat("setpoint", currentSetpoint);
    p.end();

    if (pendingBeerChange) {
        pendingBeerChange = false;
        request_dashboard_refresh = true;
        mqtt_publish_telemetry();
    }
}

void settings_set_mode(int mode) {
    currentMode = mode;
    ramp_on_mode_change(); // de volta ao fermentador com perfil ativo: alvo da etapa
    // Confirma na nuvem PRIMEIRO (a gravacao na flash pode levar dezenas de ms)
    mqtt_publish_telemetry();
    Preferences p;
    p.begin("breww", false);
    p.putInt("mode", currentMode);
    p.putFloat("setpoint", currentSetpoint);
    p.end();
}

void settings_set_setpoint(float setpoint) {
    currentSetpoint = setpoint;
    mqtt_publish_telemetry(); // app/site atualizam na hora
    schedule_save();
}

void settings_set_final_gravity(float fg) {
    finalGravity = fg;
    Preferences p;
    p.begin("breww", false);
    p.putFloat("fg", finalGravity);
    p.end();
}

void settings_set_pid_gains(double kp, double ki, double kd) {
    pidKp = kp; pidKi = ki; pidKd = kd;
    myPID.setGains(pidKp, pidKi, pidKd);
    Preferences p;
    p.begin("breww", false);
    p.putDouble("kp", pidKp);
    p.putDouble("ki", pidKi);
    p.putDouble("kd", pidKd);
    p.end();
}

void settings_set_pwm_window(int windowMs) {
    pwmWindow = windowMs;
    myPID.setOutputRange(0, pwmWindow);
    Preferences p;
    p.begin("breww", false);
    p.putInt("pwm_win", pwmWindow);
    p.end();
}

static void beer_changed() { pendingBeerChange = true; schedule_save(); }
void settings_set_beer_name(const String& name) { beerName = name; beer_changed(); }
void settings_set_beer_type(const String& type) { beerType = type; beer_changed(); }
void settings_set_beer_ibu(int ibu) { beerIBU = ibu; beer_changed(); }
void settings_set_beer_abv(float abv) { beerABV = abv; beer_changed(); }

void settings_set_beer_info(const String& name, const String& type, int ibu, float abv) {
    beerName = name.length() > 15 ? name.substring(0, 15) : name;
    beerType = type;
    beerIBU = ibu;
    beerABV = abv;
    Preferences p;
    p.begin("breww", false);
    p.putString("beerName", beerName);
    p.putString("beerType", beerType);
    p.putInt("beerIBU", beerIBU);
    p.putFloat("beerABV", beerABV);
    p.end();
    mqtt_publish_telemetry();
}
