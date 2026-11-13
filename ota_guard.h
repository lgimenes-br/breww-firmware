// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef OTA_GUARD_H
#define OTA_GUARD_H

// Atualizacao segura pelo ar. Uma versao nova (OTA da nuvem, /update ou IDE) comeca "em teste":
// so vira definitiva depois de ficar 30 s conectada a nuvem. Dois erros fatais seguidos ou 10 minutos
// sem conectar voltam para a versao anterior (um erro isolado nao derruba a atualizacao).

#include <Arduino.h>

void ota_guard_begin();                     // no inicio do setup()
void ota_guard_loop(bool cloudConnected);   // em todo loop(), com ou sem Wi-Fi
bool ota_guard_pending();                   // versao em teste (ainda nao confirmada)
bool ota_guard_rolled_back_once();          // voltou para esta versao: avisar o servidor uma vez

#endif // OTA_GUARD_H
