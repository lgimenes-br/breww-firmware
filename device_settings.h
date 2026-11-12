// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef DEVICE_SETTINGS_H
#define DEVICE_SETTINGS_H

// Configuracoes do controlador (modo, alvo, PID, dados do chopp) e a gravacao delas no NVS.
// Unico lugar que altera e grava esses valores: a tela, os comandos MQTT e (nas placas sem
// tela) o botao chamam estas funcoes. Nao depende de LVGL.

#include <Arduino.h>

void settings_load();   // le do NVS no boot (inclui migracoes antigas)
void settings_loop();   // grava as alteracoes pendentes (debounce); chamar no loop()

/** Troca o modo (fermentador/geladeira/chopp/limpeza). Confirma na nuvem e grava na hora. */
void settings_set_mode(int mode);

/** Alvo manual. Publica na hora; grava com debounce (varios toques seguidos = 1 gravacao). */
void settings_set_setpoint(float setpoint);

void settings_set_final_gravity(float fg);

/** Ganhos do PID e janela do PWM: aplicados ao controlador na hora e gravados. */
void settings_set_pid_gains(double kp, double ki, double kd);
void settings_set_pwm_window(int windowMs);

/** Dados da choppeira. Campo a campo (MQTT) ou todos de uma vez (tela). Grava com debounce. */
void settings_set_beer_name(const String& name);
void settings_set_beer_type(const String& type);
void settings_set_beer_ibu(int ibu);
void settings_set_beer_abv(float abv);
/** Todos os dados do chopp (formulario da tela): grava e publica na hora; a tela se redesenha sozinha. */
void settings_set_beer_info(const String& name, const String& type, int ibu, float abv);

#endif // DEVICE_SETTINGS_H
