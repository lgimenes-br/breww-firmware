// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef IMPROV_SERIAL_H
#define IMPROV_SERIAL_H

// Improv Serial (https://www.improv-wifi.com/serial/): o instalador do site (ESP Web Tools)
// configura o Wi-Fi pelo mesmo cabo USB logo depois de gravar o firmware. Placas sem tela.
// Pacote: "IMPROV" | versao(1) | tipo | tamanho | dados | checksum (soma dos bytes anteriores).

#include <Arduino.h>

/** Recebe um byte da serial. true = era parte de um pacote Improv (nao e texto de comando). */
bool improv_feed(uint8_t b);

/** Acompanha a conexao pedida pelo instalador e responde quando conectar ou falhar. */
void improv_loop();

#endif // IMPROV_SERIAL_H
