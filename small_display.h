// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef SMALL_DISPLAY_H
#define SMALL_DISPLAY_H

// Tela pequena I2C opcional das placas sem tela grande (Fase 2, item 8).
// Detectada no boot: OLED 128x64 (SSD1306/SH1106, 0x3C/0x3D) ou LCD 20x4 com PCF8574
// (0x27/0x3F). Sem tela ligada, nada acontece; o mesmo firmware serve com ou sem.
// O driver do OLED (SSD1306 ou SH1106) fica no NVS: comando serial "tela sh1106".

#include <Arduino.h>

struct SmallDisplayState {
    const char* modeName;   // "Fermentador", "Geladeira"...
    float temp;             // temperatura de controle
    float setpoint;
    const char* relay;      // "Resfriando" | "Aquecendo" | "Desligado"
    bool wifi;
    bool cloud;
    const char* footer;     // etapa do perfil, nome do chopp ou aviso
    const char* pairCode;   // codigo de vinculo (aparelho ainda nao vinculado) ou nullptr
    bool pairPending;       // pedido de vinculo esperando o botao
    const char* pairEmail;
    bool otaActive;
    int otaPercent;
    // Menu dos botoes (button_menu.cpp): quando menuTitle != nullptr a tela mostra o menu
    const char* menuTitle = nullptr;
    const char* menuValue = nullptr;
    const char* menuHint = nullptr;
};

/** Varre o I2C e liga a tela encontrada. Retorna o tipo ("oled", "lcd2004") ou nullptr. */
const char* small_display_begin();

/** Tipo detectado no boot ("oled", "lcd2004") ou nullptr. */
const char* small_display_kind();

/** Redesenha (limitado internamente a ~2x por segundo). */
void small_display_update(const SmallDisplayState& s);

/** Grava o driver do OLED no NVS ("ssd1306" ou "sh1106"); vale depois de reiniciar. */
bool small_display_set_oled_driver(const String& driver);

#endif // SMALL_DISPLAY_H
