// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef WIFI_PORTAL_H
#define WIFI_PORTAL_H

// Portal de configuracao do Wi-Fi para placas sem tela grande (Fase 3, item 2).
// O aparelho cria a rede aberta "Breww-XXXX" (fim do ID); o celular conecta e o sistema abre
// a pagina sozinho (portal cativo). A pessoa escolhe a rede de casa e digita a senha.
// Quando abre: sem Wi-Fi salvo, ou com Wi-Fi salvo que nao conecta ha 3 minutos.
// Quando fecha: 60 s depois de conectar (a pagina mostra o resultado e o codigo de vinculo).

#include <Arduino.h>

void wifi_portal_loop();          // chamar no loop (abre/fecha sozinho e atende o DNS/HTTP)
bool wifi_portal_active();
String wifi_portal_ssid();        // nome da rede do portal ("Breww-1A2B")

// Usados pelo servidor web (net_manager.cpp) enquanto o portal esta ativo
void wifi_portal_handle_root();
void wifi_portal_redirect();

#endif // WIFI_PORTAL_H
