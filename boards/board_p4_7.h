// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// ESP32-P4 com tela touch de 7" (Guition JC1060P470C, painel JD9165 MIPI-DSI 1024x600, touch GT911),
// Wi-Fi pelo ESP32-C6 da placa (ESP-Hosted). Build "p4-7".
// A tela usa o driver DO FABRICANTE sem alteracao (pacote JC1060P470C, painel novo: reset no GPIO5,
// luz no GPIO23). Nunca trocar a tabela de inicializacao nem os tempos do painel.
#pragma once
#define BREWW_BOARD_NAME "p4-7"
#define BREWW_HAS_LVGL 1
#define BREWW_PANEL_DSI 1

// A interface (LVGL) desenha na resolucao nativa do painel; as medidas da tela de 3,5" sao
// multiplicadas por 1,875 (ui.cpp, S()).
#define DISPLAY_SIZE 70
#define SCREEN_WIDTH 1024
#define SCREEN_HEIGHT 600
#define PANEL_WIDTH 1024
#define PANEL_HEIGHT 600
#define PANEL_LCD_RST 5      // pins_config.h do fabricante (painel novo)

// Barra de pinos 2x13 da placa (pinos livres pelo esquema)
#define PIN_ONE_WIRE_BUS 32  // DS18B20 (resistor de 4,7k entre dados e 3,3 V)
#define PIN_DHT 33           // DHT22 opcional
#define PIN_RELAY_HEATER 46  // Aquecedor
#define PIN_RELAY_COOLER 47  // Resfriador
#define PIN_BUTTON_MENU -1
#define PIN_BUTTON_ENTER -1

// Touch GT911 e conector I2C SH 4P (mesmo barramento)
#define TOUCH_SDA 7
#define TOUCH_SCL 8
#define TFT_BL 23

#define PIN_BUTTON_BOOT 35   // BOOT da placa (pino de boot do P4)
#define PIN_STATUS_LED -1
#define STATUS_LED_ACTIVE_LOW 0

// Rede cabeada (RJ45 da placa): PHY IP101 no endereco 1, RMII com o relogio de 50 MHz vindo do PHY (GPIO50).
// Pinos do exemplo "ethernet" do fabricante (MDC 31, MDIO 52, reset 51); os dados RMII usam os pinos
// padrao do P4 na biblioteca ETH (TX_EN 49, TXD0 34, TXD1 35, CRS_DV 28, RXD0 29, RXD1 30).
#define BREWW_HAS_ETH 1
#define BREWW_ETH_PHY_ADDR 1
#define BREWW_ETH_MDC 31
#define BREWW_ETH_MDIO 52
#define BREWW_ETH_PHY_RST 51

// Wi-Fi pelo C6 com antena ceramica atras da tela: ~-70 dBm mesmo ao lado do no da mesh. No limite
// padrao (-72) a placa ficava procurando outro no a toa; aqui so abaixo de -85 dBm.
#define BREWW_ROAM_WEAK_RSSI -85

// Reles: modulo comum (optoacoplador) liga em nivel baixo. SSR = normal (app: set_relays).
#define RELAY_ACTIVE_LOW_DEFAULT 1
