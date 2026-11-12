// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef I2C_SENSORS_H
#define I2C_SENSORS_H

// Sensores de umidade I2C (Fase 2, item 4): SHT3x (0x44/0x45), AHT20 (0x38) e BME280 (0x76/0x77).
// Detectados no boot no barramento I2C da placa (o mesmo da tela pequena). Mais confiaveis que
// o DHT22: quando ha um deles, ele fornece a umidade (currentHumidity) e o DHT22 e ignorado.

#include <Arduino.h>

void i2c_sensors_begin();             // depois de pins_load(); varre o I2C
void i2c_sensors_loop();              // leitura nao bloqueante a cada 5 s
const char* i2c_humidity_sensor();    // "sht3x" | "aht20" | "bme280" | nullptr
bool i2c_humidity_active();           // ha sensor I2C de umidade (o DHT22 fica de lado)

#endif // I2C_SENSORS_H
