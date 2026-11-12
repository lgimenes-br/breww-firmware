// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef ISPINDEL_H
#define ISPINDEL_H

#include <Arduino.h>

extern float ispindelGravity;
extern float ispindelTemp;
extern float ispindelBattery;

#define ISPINDEL_HISTORY_SIZE 60
extern float ispindelHistory[ISPINDEL_HISTORY_SIZE];
extern int ispindelHistoryCount;
extern int ispindelHistoryIndex;

extern unsigned long lastIspindelUpdate;
extern uint32_t lastIspindelEpoch;

void init_ispindel();
void handle_ispindel_data(float gravity, float temp, float battery);

#endif // ISPINDEL_H
