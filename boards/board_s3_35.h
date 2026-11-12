// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// Placa oficial: ESP32-S3 com tela touch de 3,5" (AXS15231B QSPI), LVGL. Padrao quando
// nenhuma placa e escolhida (compilacao pela IDE e release "s3-35").
#pragma once
#define BREWW_BOARD_NAME "s3-35"
#define BREWW_HAS_LVGL 1

// Tela (DISPLAY_SIZE 35 = 3,5")
#define DISPLAY_SIZE 35
#define SCREEN_WIDTH 480
#define SCREEN_HEIGHT 320

// Conector JST 4P (energia e sensor 1-Wire)
#define PIN_ONE_WIRE_BUS 17  // DS18B20 (pino 3 do JST 4P)
#define PIN_DHT 18           // DHT22 (pino 4 do JST 4P)

// Conector JST 8P (reles e botoes)
#define PIN_RELAY_HEATER 9   // Aquecedor (pino 1)
#define PIN_RELAY_COOLER 14  // Resfriador (pino 2)
#define PIN_BUTTON_MENU 15   // Botao 1 (pino 4)
#define PIN_BUTTON_ENTER 16  // Botao 2 (pino 5)

// Touch e backlight (usados pelo driver da tela em ui.cpp)
#define TOUCH_SDA 4
#define TOUCH_SCL 8
#define TFT_BL 1

#define PIN_BUTTON_BOOT 0
#define PIN_STATUS_LED -1    // sem LED de status: a tela mostra tudo
#define STATUS_LED_ACTIVE_LOW 0

// Polaridade padrao dos reles (placa oficial: reles da placa, ativos em alto). SSR = normal.
// Muda sem recompilar: app (comando set_relays) ou serial ("rele normal" / "rele invertido").
#define RELAY_ACTIVE_LOW_DEFAULT 0
