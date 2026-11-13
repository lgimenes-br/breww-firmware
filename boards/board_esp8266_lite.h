// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// ESP8266 (NodeMCU v2/v3, Wemos D1 mini; 4 MB de flash). Versao Lite: sem tela, sem I2C, sem botoes de
// menu, sem atualizacao local pela IDE e sem volta automatica de versao. Uma conexao segura por vez
// (a RAM do ESP8266 nao comporta duas).
//
// Pinos escolhidos para nao estalar os reles no boot: GPIO4 e GPIO5 (D2/D1) nao mudam de estado ao
// ligar. GPIO0/2/15 sao de boot, 1/3 sao a serial, 16 pulsa no reset, 6-11 sao da flash.
#pragma once
#define BREWW_BOARD_NAME "esp8266-lite"
#define BREWW_HAS_LVGL 0
#define BREWW_LITE 1

#define PIN_ONE_WIRE_BUS 14  // D5: DS18B20 (resistor de 4,7k entre dados e 3,3 V)
#define PIN_DHT 12           // D6: DHT22 opcional
#define PIN_RELAY_HEATER 5   // D1: Aquecedor
#define PIN_RELAY_COOLER 4   // D2: Resfriador

#define PIN_BUTTON_BOOT 0    // botao FLASH da NodeMCU
#define PIN_STATUS_LED 2     // LED azul do modulo (D4), acende em nivel baixo
#define STATUS_LED_ACTIVE_LOW 1

#define RELAY_ACTIVE_LOW_DEFAULT 1
