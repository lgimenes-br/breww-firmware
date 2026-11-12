// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// ESP32-C3 (ex.: C3 SuperMini), sem tela grande. Build "modular".
// Poucos pinos: 2, 8 e 9 sao de boot; 8 e o LED da placa, 9 o botao BOOT.
#pragma once
#define BREWW_BOARD_NAME "c3-headless"
#define BREWW_HAS_LVGL 0

#define PIN_ONE_WIRE_BUS 3
#define PIN_DHT 4
#define PIN_RELAY_HEATER 5
#define PIN_RELAY_COOLER 6

#define PIN_I2C_SDA 7
#define PIN_I2C_SCL 10

#define PIN_BUTTON_BOOT 9
#define PIN_STATUS_LED 8
#define STATUS_LED_ACTIVE_LOW 1  // LED da SuperMini acende em nivel baixo

// Polaridade padrao dos reles (modulo de rele comum (optoacoplador) liga em nivel baixo). SSR = normal.
// Muda sem recompilar: app (comando set_relays) ou serial ("rele normal" / "rele invertido").
#define RELAY_ACTIVE_LOW_DEFAULT 1
