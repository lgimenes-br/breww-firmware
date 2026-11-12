// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef FERMENTATION_H
#define FERMENTATION_H

// Inicio e fim de lote feitos no proprio aparelho (tela hoje; botao nas placas sem tela).
// Avisa o servidor (device_action) para o lote aparecer/encerrar no app e no site.
// Os comandos vindos da nuvem (setProfile, stopProfile...) continuam em hal_profiles.

/** Lote com perfil pronto pelo estilo ("Lager" = perfil lager; demais = ale). */
void ferm_start_preset(const char* style);

/** Encerra o perfil ativo e avisa o servidor. */
void ferm_stop_batch();

#endif // FERMENTATION_H
