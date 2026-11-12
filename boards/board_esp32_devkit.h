// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// ESP32 classico (DevKit V1 / WROOM-32), sem tela grande. Build "modular".
// Pinos escolhidos para nao usar pinos de boot (0, 2, 12, 15) em reles/sensores.
#pragma once
#define BREWW_BOARD_NAME "esp32-devkit"
#define BREWW_HAS_LVGL 0

#define PIN_ONE_WIRE_BUS 23  // DS18B20 (resistor de 4,7k entre dados e 3,3 V)
#define PIN_DHT 19           // DHT22 opcional
#define PIN_RELAY_HEATER 26  // Aquecedor
#define PIN_RELAY_COOLER 27  // Resfriador

#define PIN_I2C_SDA 21       // tela pequena I2C / sensores I2C (Fase 2, item 8)
#define PIN_I2C_SCL 22

#define PIN_BUTTON_BOOT 0    // botao BOOT da placa
#define PIN_STATUS_LED 2     // LED azul da placa
#define STATUS_LED_ACTIVE_LOW 0

// Polaridade padrao dos reles (modulo de rele comum (optoacoplador) liga em nivel baixo). SSR = normal.
// Muda sem recompilar: app (comando set_relays) ou serial ("rele normal" / "rele invertido").
#define RELAY_ACTIVE_LOW_DEFAULT 1
