// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>

// Modos de Operacao
enum OperationMode {
    MODE_FERM = 0,
    MODE_GELADEIRA = 1,
    MODE_CHOPP = 2,
    MODE_CLEAN = 3
};

// Variaveis de Estado (declaradas externamente)
extern float currentTemp;
extern float currentTemp2;
extern float currentHumidity;
extern float currentSetpoint;
extern float finalGravity;
extern int currentMode;
extern bool isUpdatingOTA;
extern String beerName;
extern String beerType;
extern int beerIBU;
extern float beerABV;
extern String profileStatus;
extern bool request_dashboard_refresh; // pede para a tela remontar a pagina do modo atual



// Variaveis do Relé
extern bool isHeaterOn;
extern bool isCoolerOn;


// Estrutura de Rampas
struct RampStep {
    float durationDays;   // pode ser fracionado (ex.: 1.5 dia); o app/site mandam em horas
    float targetTemp;
    char name[20];
};

#define MAX_RAMP_STEPS 10
extern RampStep rampSteps[MAX_RAMP_STEPS];
extern int numRampSteps;
extern int currentRampStep;
extern unsigned long rampStepStartUnix; // Salvo em NVS para sobreviver a reboots
extern bool isRampActive;
// Pausa do perfil: o tempo da etapa para de contar (comando togglePause do app/site)
extern bool isRampPaused;
extern unsigned long rampPausedAtUnix;
// setProfile recebido antes do NTP: aplica o tempo decorrido quando o relogio sincronizar
extern long rampPendingElapsedSec;

#endif // GLOBALS_H

// PID Variables
extern double pidInput, pidOutput, pidSetpoint;
extern double pidKp, pidKi, pidKd;
extern int pwmWindow;
