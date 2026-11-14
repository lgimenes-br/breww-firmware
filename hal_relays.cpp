// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "hal_relays.h"
#include "board_pins.h"
#include "hal_sensors.h"
#include "platform.h"
#include <AutoPID.h>

unsigned long lastCompressorOffTime = 0;
unsigned long windowStartTime = 0;

AutoPID myPID(&pidInput, &pidSetpoint, &pidOutput, 0, 2000, pidKp, pidKi, pidKd);

// --- Polaridade dos reles (Fase 2, item 5) ---
// Rele "invertido" = ativo em nivel BAIXO (a maioria dos modulos de rele com optoacoplador).
// SSR e a placa oficial sao ativos em nivel ALTO. Fica no NVS; padrao vem da placa.
static bool heaterInverted = RELAY_ACTIVE_LOW_DEFAULT;
static bool coolerInverted = RELAY_ACTIVE_LOW_DEFAULT;

static inline void relay_write(int pin, bool on) {
    bool inv = (pin == g_pins.heater) ? heaterInverted : coolerInverted;
    digitalWrite(pin, (on != inv) ? HIGH : LOW);
}

bool relay_heater_inverted() { return heaterInverted; }
bool relay_cooler_inverted() { return coolerInverted; }

void relays_set_inverted(bool heater, bool cooler) {
    heaterInverted = heater;
    coolerInverted = cooler;
    Preferences p;
    p.begin("breww", false);
    p.putBool("rel_inv_h", heaterInverted);
    p.putBool("rel_inv_c", coolerInverted);
    p.end();
    // Reaplica o estado atual com a polaridade nova (desligado continua desligado)
    relay_write(g_pins.heater, isHeaterOn);
    relay_write(g_pins.cooler, isCoolerOn);
    Serial.printf_P(PSTR("[Reles] Polaridade: aquecer %s, resfriar %s\n"),
                  heaterInverted ? "invertido (ativo em baixo)" : "normal (ativo em alto)",
                  coolerInverted ? "invertido (ativo em baixo)" : "normal (ativo em alto)");
}


// --- Auto-Tune (Ziegler-Nichols) ---
bool isAutoTuning = false;
String autotuneMsg = "";
int tuneState = 0; 
int tuneCycles = 0;
unsigned long cycleStartTime = 0;
float maxTemp = -100;
float minTemp = 100;
unsigned long periods[4];
float amplitudes[4];

void start_autotune() {
    if (isAutoTuning) return;
    isAutoTuning = true;
    tuneState = 1; // 1 = Aquecendo, 2 = Esfriando (inercia)
    tuneCycles = 0;
    float controlTemp = control_temp();
    maxTemp = controlTemp;
    minTemp = controlTemp;
    cycleStartTime = millis();
    autotuneMsg = "Iniciando (Aquecendo)";
    relay_write(g_pins.heater, true);
    isHeaterOn = true;
    relay_write(g_pins.cooler, false);
    isCoolerOn = false;
    Serial.println(F("[AutoTune] Iniciado."));
}

void process_autotune() {
    float controlTemp = control_temp();

    if (controlTemp > maxTemp) maxTemp = controlTemp;
    if (controlTemp < minTemp) minTemp = controlTemp;

    if (tuneState == 1) { // Aquecendo
        if (controlTemp >= currentSetpoint) {
            // Cruzou para cima
            relay_write(g_pins.heater, false);
            isHeaterOn = false;
            tuneState = 2; // Espera esfriar
            autotuneMsg = "Aguardando inercia (Resfriando)";
            Serial.println(F("[AutoTune] Cruzou pra cima. Cortando calor."));
        }
    } else if (tuneState == 2) { // Esfriando
        if (controlTemp <= currentSetpoint) {
            // Cruzou para baixo
            relay_write(g_pins.heater, true);
            isHeaterOn = true;
            tuneState = 1; // Volta a aquecer
            
            // Registra os dados deste ciclo
            if (tuneCycles < 4) {
                periods[tuneCycles] = millis() - cycleStartTime;
                amplitudes[tuneCycles] = (maxTemp - minTemp) / 2.0;
            }
            cycleStartTime = millis();
            maxTemp = controlTemp;
            minTemp = controlTemp;
            tuneCycles++;
            autotuneMsg = "Ciclo " + String(tuneCycles) + " concluido";
            Serial.printf_P(PSTR("[AutoTune] Ciclo %d concluido.\n"), tuneCycles);

            if (tuneCycles >= 3) { // 3 ciclos sao suficientes
                isAutoTuning = false;
                relay_write(g_pins.heater, false);
                isHeaterOn = false;
                
                // Calcula Ziegler-Nichols (Ku e Tu)
                float avgAmp = (amplitudes[1] + amplitudes[2]) / 2.0;
                float avgPer = (periods[1] + periods[2]) / 2000.0; // Em segundos
                
                if (avgAmp > 0) {
                    float Ku = (4.0 * pwmWindow) / (3.14159 * avgAmp);
                    float Tu = avgPer;
                    
                    pidKp = 0.6 * Ku;
                    pidKi = (1.2 * Ku) / Tu;
                    pidKd = (0.075 * Ku) * Tu;
                    
                    // Salvar na memoria (NVS)
                    Preferences p;
                    p.begin("breww", false);
                    p.putDouble("kp", pidKp);
                    p.putDouble("ki", pidKi);
                    p.putDouble("kd", pidKd);
                    p.end();
                    
                    myPID.setGains(pidKp, pidKi, pidKd);
                    autotuneMsg = "Sucesso! Kp/Ki/Kd salvos.";
                    Serial.println(F("[AutoTune] Finalizado com sucesso."));
                } else {
                    autotuneMsg = "Falhou (Amplitude 0)";
                    Serial.println(F("[AutoTune] Falhou."));
                }
            }
        }
    }
}

void init_relays() {
    Serial.println(F("Inicializando modulo de Reles e PID..."));
    Preferences p;
    p.begin("breww", true);
    heaterInverted = p.getBool("rel_inv_h", RELAY_ACTIVE_LOW_DEFAULT);
    coolerInverted = p.getBool("rel_inv_c", RELAY_ACTIVE_LOW_DEFAULT);
    p.end();

    // Nivel de "desligado" ANTES de virar saida: rele invertido nao pisca ligado no boot
    relay_write(g_pins.heater, false);
    relay_write(g_pins.cooler, false);
    pinMode(g_pins.heater, OUTPUT);
    pinMode(g_pins.cooler, OUTPUT);
    relay_write(g_pins.heater, false);
    relay_write(g_pins.cooler, false);
    Serial.printf_P(PSTR("[Reles] aquecer %s, resfriar %s\n"), heaterInverted ? "invertido" : "normal", coolerInverted ? "invertido" : "normal");
    
    isHeaterOn = false;
    isCoolerOn = false;
    
    lastCompressorOffTime = millis();
    windowStartTime = millis();
    
    myPID.setBangBang(2.0, 0.5); // Forca 100% ou 0% se estiver muito longe (Heater)
    myPID.setTimeStep(500); // Atualiza o calculo a cada 500ms
}

void process_thermostat() {
    if (isAutoTuning && control_sensor_ok()) {
        process_autotune();
        return;
    }

    float controlTemp = control_temp();
    
    // Sincronizar variaveis de entrada do PID com o sistema real
    pidInput = controlTemp;
    pidSetpoint = currentSetpoint;
    myPID.setOutputRange(0, pwmWindow); // Caso mudem no app

    // MODO LIMPEZA (Manual/Desligado)
    if (currentMode == MODE_CLEAN) {
        pidOutput = 0;
        relay_write(g_pins.heater, false);
        relay_write(g_pins.cooler, false);
        isHeaterOn = false;
        isCoolerOn = false;
        return;
    }

    // Sem leitura valida do sensor de controle: nada de aquecer ou resfriar "no escuro".
    // Antes, sem sensor, a temperatura ficava em 0 °C e o modo fermentacao ligava o aquecedor sem parar.
    {
        static bool warned = false;
        if (!control_sensor_ok()) {
            pidOutput = 0;
            relay_write(g_pins.heater, false);
            relay_write(g_pins.cooler, false);
            isHeaterOn = false;
            isCoolerOn = false;
            if (isAutoTuning) { isAutoTuning = false; autotuneMsg = "Cancelado: sem sensor"; }
            if (!warned) { warned = true; Serial.println(F("[Seguranca] Sem leitura do sensor de controle: reles desligados ate o sensor responder.")); }
            return;
        }
        if (warned) { warned = false; Serial.println(F("[Seguranca] Sensor de controle voltou: controle retomado.")); }
    }

    // MODO CHOPP (Kegerator)
    if (currentMode == MODE_CHOPP) {
        pidOutput = 0;
        // Desliga aquecedor permanentemente
        relay_write(g_pins.heater, false);
        isHeaterOn = false;

        float erro = controlTemp - currentSetpoint;
        bool requestCooler = false;

        // Histerese mais agressiva para chopp (menor variacao)
        if (erro > 0.3) {
            requestCooler = true;
        } else if (erro < -0.1) {
            requestCooler = false;
        } else {
            requestCooler = isCoolerOn; // mantem estado
        }

        if (requestCooler) {
            if (millis() - lastCompressorOffTime > (COMPRESSOR_DELAY_SEC * 1000)) {
                relay_write(g_pins.cooler, true);
                isCoolerOn = true;
            }
        } else {
            if (isCoolerOn) lastCompressorOffTime = millis();
            relay_write(g_pins.cooler, false);
            isCoolerOn = false;
        }
        return;
    }

    // Se estiver no modo Fermentacao ou Geladeira, continua abaixo (Controle PID)

    // Calcula PID (O output vai de 0 ate pwmWindow)
    myPID.run();

    // ==========================================
    // 1. CONTROLE DO AQUECEDOR (HEATER) VIA PWM
    // ==========================================
    // Usa apenas a logica de PWM se o erro pedir aquecimento
    if (currentSetpoint > controlTemp - 0.2) { // Evita aquecer e resfriar perto do alvo
        unsigned long now = millis();
        if (now - windowStartTime > pwmWindow) {
            windowStartTime += pwmWindow;
        }
        
        // Pulso minimo (rele mecanico, janela >= 10 s): perto do alvo o PID pede pulsos de poucos ms,
        // que so fazem o rele estalar a cada janela sem aquecer nada. Abaixo do minimo fica desligado
        // (o termo integral cresce ate valer um pulso de verdade); perto de 100% fica ligado direto.
        const double minPulse = pwmWindow >= 10000 ? max(1000.0, pwmWindow * 0.05) : 0.0;
        double duty = pidOutput;
        if (duty < minPulse) duty = 0;
        else if (duty > pwmWindow - minPulse) duty = pwmWindow;

        // Previne os "tics" (rele piscando em 0% e 100%) filtrando os extremos
        if (duty >= pwmWindow) {
            // Fixo em 100% (Saturado)
            if (!isHeaterOn) {
                relay_write(g_pins.heater, true);
                isHeaterOn = true;
            }
        } else if (duty <= 0) {
            // Fixo em 0%
            if (isHeaterOn) {
                relay_write(g_pins.heater, false);
                isHeaterOn = false;
            }
        } else {
            // PWM Modulado (entre 1% e 99%)
            if (duty > (now - windowStartTime)) {
                if (!isHeaterOn) {
                    relay_write(g_pins.heater, true);
                    isHeaterOn = true;
                }
            } else {
                if (isHeaterOn) {
                    relay_write(g_pins.heater, false);
                    isHeaterOn = false;
                }
            }
        }
    } else {
        pidOutput = 0; // Desliga o PWM visualmente
        relay_write(g_pins.heater, false);
        isHeaterOn = false;
    }

    // ==========================================
    // 2. CONTROLE DO RESFRIADOR (HISTERESE)
    // ==========================================
    // PID nao funciona em compressores! Usamos Histerese + Atraso de 5 min
    float erro = controlTemp - currentSetpoint;
    bool requestCooler = false;

    if (erro > DEFAULT_HISTERESE) {
        requestCooler = true;
    }

    // Seguranca: Nunca ligar ambos ao mesmo tempo (Interlock)
    if (isHeaterOn && requestCooler) {
        requestCooler = false;
    }

    if (requestCooler) {
        // So liga se tiver passado o tempo de delay desde a ultima vez q desligou
        if (millis() - lastCompressorOffTime > (COMPRESSOR_DELAY_SEC * 1000)) {
            relay_write(g_pins.cooler, true);
            isCoolerOn = true;
        }
    } else {
        if (isCoolerOn) {
            // Acabou de desligar, marcar o tempo
            lastCompressorOffTime = millis();
        }
        relay_write(g_pins.cooler, false);
        isCoolerOn = false;
    }
}
void override_relays(bool heaterState, bool coolerState) {
    // Usado pelo PID ou pelo app para controle manual
    relay_write(g_pins.heater, heaterState);
    relay_write(g_pins.cooler, coolerState);
    isHeaterOn = heaterState;
    isCoolerOn = coolerState;
}
