// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "globals.h"

// Inicializacao das variaveis globais
float currentTemp = 0.0;
float currentTemp2 = 0.0;
float currentHumidity = NAN; // NAN = sem leitura valida do DHT22 (publicado como null)
float currentSetpoint = 18.0;
float finalGravity = 1.010;
int currentMode = MODE_GELADEIRA; // placa nova comeca como geladeira (settings_load grava)
String beerName = "MINHA BREJA";
String profileStatus = "AGUARDANDO";


bool isHeaterOn = false;
bool isCoolerOn = false;

// PID Variables
double pidInput = 0, pidOutput = 0, pidSetpoint = 0;
double pidKp = 200.0, pidKi = 1.0, pidKd = 0.0;
int pwmWindow = 2000;

// Variaveis de Rampa
RampStep rampSteps[MAX_RAMP_STEPS] = {
    {3, 18.0, "Primaria"},
    {2, 22.0, "Diacetil"},
    {5, 2.0, "Cold Crash"},
};
int numRampSteps = 3;
int currentRampStep = 0;
unsigned long rampStepStartUnix = 0;
bool isRampActive = false;
bool isRampPaused = false;
unsigned long rampPausedAtUnix = 0;
long rampPendingElapsedSec = -1;
String beerType = "IPA";
int beerIBU = 40;
float beerABV = 5.5;
bool isUpdatingOTA = false;

// A tela remonta a pagina do modo atual no proximo ciclo (sem tela: ignorado)
bool request_dashboard_refresh = false;
