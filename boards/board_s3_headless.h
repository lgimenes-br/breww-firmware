// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// ESP32-S3 sem tela grande (ex.: ESP32-S3-DevKitC). Build "modular".
// Mesmos pinos de sensores e reles da placa oficial, para facilitar a montagem.
#pragma once
#define BREWW_BOARD_NAME "s3-headless"
#define BREWW_HAS_LVGL 0

#define PIN_ONE_WIRE_BUS 17
#define PIN_DHT 18
#define PIN_RELAY_HEATER 9
#define PIN_RELAY_COOLER 14

#define PIN_I2C_SDA 4
#define PIN_I2C_SCL 5

#define PIN_BUTTON_BOOT 0
#define PIN_STATUS_LED -1    // o DevKitC tem LED RGB (WS2812) no 48: nao usado por enquanto
#define STATUS_LED_ACTIVE_LOW 0

// Polaridade padrao dos reles (modulo de rele comum (optoacoplador) liga em nivel baixo). SSR = normal.
// Muda sem recompilar: app (comando set_relays) ou serial ("rele normal" / "rele invertido").
#define RELAY_ACTIVE_LOW_DEFAULT 1
