// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "ispindel.h"
#include <Preferences.h>
#include <time.h>

float ispindelGravity = 0.0;
float ispindelTemp = 0.0;
float ispindelBattery = 0.0;

float ispindelHistory[ISPINDEL_HISTORY_SIZE];
int ispindelHistoryCount = 0;
int ispindelHistoryIndex = 0;

unsigned long lastIspindelUpdate = 0;
uint32_t lastIspindelEpoch = 0;

void init_ispindel() {
    Preferences p;
    p.begin("ispindel", true);
    ispindelGravity = p.getFloat("sg", 0.0);
    ispindelTemp = p.getFloat("tmp", 0.0);
    ispindelBattery = p.getFloat("bat", 0.0);
    lastIspindelEpoch = p.getUInt("epc", 0);
    ispindelHistoryCount = p.getInt("hc", 0);
    ispindelHistoryIndex = p.getInt("hi", 0);
    if(p.getBytesLength("hist") == sizeof(ispindelHistory)) {
        p.getBytes("hist", ispindelHistory, sizeof(ispindelHistory));
    }
    p.end();
}

void handle_ispindel_data(float gravity, float temp, float battery) {
    ispindelGravity = gravity;
    ispindelTemp = temp;
    ispindelBattery = battery;
    lastIspindelUpdate = millis();
    lastIspindelEpoch = (uint32_t)time(NULL);
    
    // Atualiza o historico para o grafico
    ispindelHistory[ispindelHistoryIndex] = gravity;
    ispindelHistoryIndex = (ispindelHistoryIndex + 1) % ISPINDEL_HISTORY_SIZE;
    if (ispindelHistoryCount < ISPINDEL_HISTORY_SIZE) {
        ispindelHistoryCount++;
    }
    
    Preferences p;
    p.begin("ispindel", false);
    p.putFloat("sg", gravity);
    p.putFloat("tmp", temp);
    p.putFloat("bat", battery);
    p.putUInt("epc", lastIspindelEpoch);
    p.putInt("hc", ispindelHistoryCount);
    p.putInt("hi", ispindelHistoryIndex);
    p.putBytes("hist", ispindelHistory, sizeof(ispindelHistory));
    p.end();
    
    Serial.printf("iSpindel Atualizado -> SG: %.3f | Temp: %.1fC | Bat: %.2fV\n", gravity, temp, battery);
}
