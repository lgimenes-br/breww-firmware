// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef BOARD_PINS_H
#define BOARD_PINS_H

// Pinos da placa (Fase 2, item 3). Padrao = boards/<placa>.h. Nas placas sem tela grande podem
// ser trocados sem recompilar (NVS), com validacao por chip; na s3-35 sao fixos (conectores da placa).
// Mudanca vale depois de reiniciar.

#include <Arduino.h>

struct BoardPins {
    int8_t oneWire;  // DS18B20 (obrigatorio)
    int8_t dht;      // DHT22 (-1 = sem)
    int8_t heater;   // rele de aquecer (obrigatorio)
    int8_t cooler;   // rele de resfriar (obrigatorio)
    int8_t sda;      // I2C: tela pequena e sensores I2C (-1 = sem I2C)
    int8_t scl;
    int8_t btnUp;    // botoes do menu da tela pequena (-1 = sem)
    int8_t btnDown;
};

extern BoardPins g_pins;

void pins_load();                                   // chamar no inicio do setup()
bool pins_configurable();                           // false na s3-35
bool pins_validate(const BoardPins& p, String& err);
bool pins_save(const BoardPins& p, String& err);    // valida e grava (vale apos reiniciar)
void pins_reset_defaults();
void pins_to_json(char* out, size_t n);

// Pino livre e seguro para procurar sensor (DS18B20/DHT22) sozinho: existe, nao e flash/USB/serial,
// nao e pino de boot, LED nem BOOT, e nao esta em uso por rele, I2C ou botao.
bool pins_sensor_candidate(int p);
const int PINS_MAX_GPIO = 48;

// Placa de tela (pinos fixos): DS18B20 e DHT22 invertidos no JST 4P (cabo feito a mao).
// A troca fica gravada e vale nos proximos boots.
void pins_set_sensor_swap(bool swapped);             // {"ow":23,"dht":19,...}

// Ultimo erro de set_pins (vai na telemetria por 60 s para o app mostrar)
const char* pins_last_error();
void pins_set_last_error(const String& err);

#endif // BOARD_PINS_H
