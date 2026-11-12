// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef UI_H
#define UI_H

#include <Arduino.h>

// Interface da tela. Implementada por ui.cpp (tela grande, LVGL) ou por ui_headless.cpp
// (placas sem tela grande: LED, botao BOOT e serial). A logica chama so estas funcoes.

// Funcao principal que o main.ino vai chamar
void init_ui();
void loop_ui();

// Funcoes internas de atualizacao
void ui_update_temperatures();
void ui_update_relays();
void ui_update_ispindel();

void ui_show_ota_screen();
void ui_update_ota_progress(int percent);
void ui_show_ota_success(bool success);
void ui_ota_status(const char* text); // troca o subtitulo (ex: "Tentativa 2 de 3...")
void ui_hide_ota_screen();            // remove a sobreposicao (falha definitiva)

// Confirmacao de vinculo com uma conta (pedido feito no app)
void ui_show_pair_confirm(const char* email, const char* deviceName, int expiresIn);
void ui_hide_pair_confirm();
// status: "confirmed" | "rejected" | "expired" | "conflict" | "limit" | ""
void ui_pair_result(const char* status, bool requestOk);

#endif // UI_H
