// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef HAL_SENSORS_H
#define HAL_SENSORS_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include <Preferences.h>
#include "config.h"
#include "globals.h"

void init_sensors();
void read_temperatures();

// Mapeamento DS18B20 por MAC (hex de 16 chars, vazio = automatico pela ordem do barramento)
extern String sensorMacCtrl;
extern String sensorMac2;
void set_sensor_mapping(const String& macCtrl, const String& mac2);
// Re-enumera o barramento e faz uma leitura fresca. Retorna quantos sensores achou.
int scan_ds18b20(String macs[], float temps[], int maxCount);
// Quantos DS18B20 estao no barramento agora (hardware detectado, Fase 2 item 9)
int ds18b20_count();

#endif // HAL_SENSORS_H
