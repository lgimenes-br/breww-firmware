// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef CRASH_REPORT_H
#define CRASH_REPORT_H

// Ultimo travamento (erro fatal) para o diagnostico remoto. No ESP32 le o resumo do core dump gravado
// na flash (tarefa, PC, causa, pilha de chamadas e o SHA do firmware que travou) e apaga depois de
// enfileirar; no ESP8266 usa as informacoes do reinicio (causa, EPC, endereco). Vai como evento
// {"type":"crash",...} no topico brewbrother/<ID>/diag. Os enderecos sao decodificados com o .elf
// da versao (anexado na Release do GitHub).

void crash_report_check(); // no setup(), depois do ota_guard_begin()

#endif // CRASH_REPORT_H
