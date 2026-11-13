// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef WIFI_SETUP_H
#define WIFI_SETUP_H

// Acompanhamento da conexao Wi-Fi do assistente (tela) e, nas placas sem tela, do portal.
// Rede nova que falha em 15 s tem a senha apagada (volta para a escolha da rede); a rede
// salva que falha no boot e mantida (o roteador pode so estar desligado).

#include <Arduino.h>

enum WifiSetupState { WIFI_SETUP_IDLE, WIFI_SETUP_CONNECTING, WIFI_SETUP_CONNECTED, WIFI_SETUP_FAILED };

void wifi_setup_connect_new(const String& ssid, const String& pass); // grava e conecta
void wifi_setup_track_saved();   // boot: acompanha a conexao ja iniciada com a rede salva
bool wifi_setup_connecting();     // conectando numa rede nova agora (o vigia do Wi-Fi nao interfere)
WifiSetupState wifi_setup_poll(); // avanca o estado; chamar no loop enquanto CONNECTING
bool wifi_setup_is_new_network(); // a tentativa atual e de uma rede nova (assistente)
void wifi_setup_reset();          // volta para IDLE depois de tratar CONNECTED/FAILED

#endif // WIFI_SETUP_H
