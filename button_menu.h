// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef BUTTON_MENU_H
#define BUTTON_MENU_H

// Menu pelos botoes da tela pequena (Fase 2, item 10), no estilo do BrewPiLess:
//  - tela principal: SUBIR/DESCER ajusta o alvo (+-0,5; segurando repete); 3 s sem toque grava
//  - os dois juntos: menu Modo / Alvo / Perfil (pausar/retomar) / Sair; os dois juntos confirmam
//  - 10 s sem uso volta para a tela principal
// Pinos: g_pins.btnUp / btnDown (-1 = sem botoes; configuraveis pelo app ou serial).

#include <Arduino.h>
#include "small_display.h"

void menu_begin();
void menu_loop();
bool menu_active();
void menu_fill(SmallDisplayState& st); // preenche o que a tela mostra enquanto o menu esta aberto

#endif // BUTTON_MENU_H
