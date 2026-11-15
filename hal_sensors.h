// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef HAL_SENSORS_H
#define HAL_SENSORS_H

#include <Arduino.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <DHT.h>
#include "platform.h"
#include "config.h"
#include "globals.h"

void init_sensors();
void read_temperatures();

// Sensores numerados: Sensor 1..4 = slot 0..3, cada um preso ao MAC (hex de 16 chars, vazio = automatico)
#define SENSOR_SLOTS 4
extern String sensorMacs[SENSOR_SLOTS];
extern String& sensorMacCtrl; // = sensorMacs[0] (nome antigo)
extern String& sensorMac2;    // = sensorMacs[1]
extern float sensorTemps[SENSOR_SLOTS]; // NAN = sem leitura
// Sensor que controla cada modo: [0] fermentador, [1] geladeira, [2] chopp. -1 = automatico.
extern int8_t ctlMap[3];
void set_sensor_slots(const String macs[SENSOR_SLOTS]);
void set_sensor_mapping(const String& macCtrl, const String& mac2); // formato antigo (slots 1 e 2)
void set_control_map(int ferm, int fridge, int chopp);
// Re-enumera o barramento e faz uma leitura fresca. Retorna quantos sensores achou.
int scan_ds18b20(String macs[], float temps[], int maxCount);
// Quantos DS18B20 estao no barramento agora (hardware detectado, Fase 2 item 9)
int ds18b20_count();
// Leitura valida recente de um slot. Sem leitura ha 15 s (sensor ausente, solto ou com defeito)
// o termostato desliga os reles se for o sensor de controle.
bool sensor_fresh(int slot);
// Sensor de controle do modo atual: o escolhido em ctlMap; em automatico, fermentacao pelo Sensor 1 e
// geladeira/chopp pelo Sensor 2 (sem ele, pelo 1: placa com um sensor so funciona em qualquer modo).
int control_slot();
float control_temp();
bool control_sensor_ok();
const unsigned long SENSOR_STALE_MS = 15000;

#endif // HAL_SENSORS_H
