// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "hal_profiles.h"
#include <time.h>
#include "platform.h"

// Motor de rampas (perfil de fermentacao) do controlador.
// - As etapas ficam salvas no NVS ("breww": ramp_n + ramp_steps) e sobrevivem a reinicios.
// - O tempo da etapa conta a partir de rampStepStartUnix; com pausa, para de contar.
// - Ao fim da ultima etapa o perfil segue ativo, segurando a temperatura da ultima etapa
//   (mesmo comportamento do app/site e do servidor).
// - O setpoint so e aplicado pelo perfil no modo fermentador.
// Comandos remotos (app/site/servidor): setProfile, skipStep, togglePause, stopProfile.

static const unsigned long VALID_EPOCH = 1000000000UL;

static bool clock_valid(time_t *out) {
    time_t now;
    time(&now);
    if (out) *out = now;
    return now >= (time_t)VALID_EPOCH;
}

static void save_ramp_state() {
    Preferences p;
    p.begin("breww", false);
    p.putBool("ramp_active", isRampActive);
    p.putInt("ramp_step", currentRampStep);
    p.putULong("ramp_start", rampStepStartUnix);
    p.putBool("ramp_paused", isRampPaused);
    p.putULong("ramp_pause_at", rampPausedAtUnix);
    p.putFloat("setpoint", currentSetpoint);
    p.end();
}

void save_ramp_steps() {
    Preferences p;
    p.begin("breww", false);
    p.putInt("ramp_n", numRampSteps);
    p.putBytes("ramp_steps", rampSteps, sizeof(RampStep) * MAX_RAMP_STEPS);
    p.end();
}

void init_profiles() {
    Preferences p;
    p.begin("breww", true);
    isRampActive = p.getBool("ramp_active", false);
    currentRampStep = p.getInt("ramp_step", 0);
    rampStepStartUnix = p.getULong("ramp_start", 0);
    isRampPaused = p.getBool("ramp_paused", false);
    rampPausedAtUnix = p.getULong("ramp_pause_at", 0);
    int n = p.getInt("ramp_n", -1);
    if (n > 0 && n <= MAX_RAMP_STEPS && p.getBytesLength("ramp_steps") == sizeof(RampStep) * MAX_RAMP_STEPS) {
        p.getBytes("ramp_steps", rampSteps, sizeof(RampStep) * MAX_RAMP_STEPS);
        numRampSteps = n;
    }
    p.end();
    if (currentRampStep < 0) currentRampStep = 0;
    if (numRampSteps > 0 && currentRampStep >= numRampSteps) currentRampStep = numRampSteps - 1;
}

/** Aplica a temperatura da etapa atual (so no modo fermentador) */
static void apply_step_setpoint() {
    if (currentMode == MODE_FERM && numRampSteps > 0 && currentRampStep < numRampSteps) {
        currentSetpoint = rampSteps[currentRampStep].targetTemp;
    }
}

void start_ramp_profile() {
    time_t now;
    if (!clock_valid(&now)) {
        // NTP ainda nao sincronizou, ignorar start por enquanto
        Serial.println(F("[Rampas] Aguardando NTP para iniciar rampa..."));
        return;
    }

    currentRampStep = 0;
    rampStepStartUnix = (unsigned long)now;
    isRampActive = true;
    isRampPaused = false;
    rampPausedAtUnix = 0;
    rampPendingElapsedSec = -1;

    // Atualiza o setpoint imediatamente para a primeira etapa
    if (numRampSteps > 0) {
        currentSetpoint = rampSteps[0].targetTemp;
    }
    save_ramp_steps();
    save_ramp_state();
}

/** Horas decorridas na etapa atual (pausa nao conta) */
float ramp_step_elapsed_hours() {
    if (!isRampActive) return 0;
    if (rampPendingElapsedSec >= 0) return rampPendingElapsedSec / 3600.0f;
    time_t now;
    if (!clock_valid(&now) || rampStepStartUnix == 0) return 0;
    unsigned long ref = isRampPaused && rampPausedAtUnix ? rampPausedAtUnix : (unsigned long)now;
    if (ref < rampStepStartUnix) return 0;
    return (ref - rampStepStartUnix) / 3600.0f;
}

void process_ramps() {
    if (!isRampActive || numRampSteps == 0) return;

    time_t now;
    if (!clock_valid(&now)) return; // Esperar NTP

    // setProfile chegou antes do relogio: agora da para fixar o inicio da etapa
    if (rampPendingElapsedSec >= 0) {
        rampStepStartUnix = (unsigned long)now - (unsigned long)rampPendingElapsedSec;
        if (isRampPaused) rampPausedAtUnix = (unsigned long)now;
        rampPendingElapsedSec = -1;
        save_ramp_state();
    }

    if (isRampPaused) return; // pausado: o tempo da etapa nao anda
    if (currentRampStep >= numRampSteps - 1) return; // ultima etapa: segura a temperatura

    unsigned long durationSeconds = (unsigned long)(rampSteps[currentRampStep].durationDays * 86400.0f);
    if ((unsigned long)now - rampStepStartUnix >= durationSeconds) {
        // Etapa concluida, ir para a proxima
        currentRampStep++;
        rampStepStartUnix = (unsigned long)now;
        apply_step_setpoint();
        save_ramp_state();
        request_dashboard_refresh = true;
        Serial.printf_P(PSTR("[Rampas] Avancou para etapa %d: %.1f C\n"), currentRampStep, rampSteps[currentRampStep].targetTemp);
    }
}

// ---------------------------------------------------------------------------
// Comandos remotos
// ---------------------------------------------------------------------------

/** Copia o nome cortando sem quebrar um caractere UTF-8 no meio */
static void copy_step_name(char *dst, size_t size, const char *src, int index) {
    if (!src || !*src) {
        snprintf_P(dst, size, PSTR("Etapa %d"), index + 1);
        return;
    }
    size_t len = strlen(src);
    if (len >= size) {
        len = size - 1;
        while (len > 0 && (((unsigned char)src[len]) & 0xC0) == 0x80) len--; // volta ao inicio do caractere
    }
    memcpy(dst, src, len);
    dst[len] = '\0';
}

bool ramp_set_profile(JsonArrayConst steps, int stepIndex, float stepTimeHours) {
    int n = 0;
    RampStep parsed[MAX_RAMP_STEPS];
    memset(parsed, 0, sizeof(parsed));
    for (JsonObjectConst s : steps) {
        if (n >= MAX_RAMP_STEPS) break;
        float t = s["t"] | NAN;
        float hours = s["d"] | 0.0f;
        if (isnan(t) || t < -10 || t > 50) continue; // temperatura impossivel: ignora a etapa
        parsed[n].targetTemp = t;
        parsed[n].durationDays = hours > 0 ? hours / 24.0f : 1.0f;
        copy_step_name(parsed[n].name, sizeof(parsed[n].name), s["n"] | "", n);
        n++;
    }
    if (n == 0) {
        Serial.println(F("[Rampas] setProfile sem etapas validas, ignorado"));
        return false;
    }

    memcpy(rampSteps, parsed, sizeof(rampSteps));
    numRampSteps = n;
    if (stepIndex < 0) stepIndex = 0;
    if (stepIndex >= n) stepIndex = n - 1;
    currentRampStep = stepIndex;
    if (stepTimeHours < 0 || isnan(stepTimeHours)) stepTimeHours = 0;

    long elapsedSec = (long)(stepTimeHours * 3600.0f);
    time_t now;
    if (clock_valid(&now)) {
        rampStepStartUnix = (unsigned long)now - (unsigned long)elapsedSec;
        if (isRampPaused) rampPausedAtUnix = (unsigned long)now; // pausado continua pausado
        rampPendingElapsedSec = -1;
    } else {
        rampPendingElapsedSec = elapsedSec;
    }
    isRampActive = true;
    apply_step_setpoint();
    save_ramp_steps();
    save_ramp_state();
    request_dashboard_refresh = true;
    Serial.printf_P(PSTR("[Rampas] Perfil recebido: %d etapas, etapa %d, %.1f h decorridas\n"), n, stepIndex, stepTimeHours);
    return true;
}

bool ramp_skip_step() {
    if (!isRampActive || currentRampStep >= numRampSteps - 1) return false;
    time_t now;
    bool valid = clock_valid(&now);
    currentRampStep++;
    rampStepStartUnix = valid ? (unsigned long)now : 0;
    if (isRampPaused && valid) rampPausedAtUnix = (unsigned long)now;
    rampPendingElapsedSec = valid ? -1 : 0;
    apply_step_setpoint();
    save_ramp_state();
    request_dashboard_refresh = true;
    Serial.printf_P(PSTR("[Rampas] Etapa avancada manualmente para %d\n"), currentRampStep);
    return true;
}

bool ramp_toggle_pause() {
    if (!isRampActive) return false;
    time_t now;
    bool valid = clock_valid(&now);
    if (!isRampPaused) {
        isRampPaused = true;
        rampPausedAtUnix = valid ? (unsigned long)now : 0;
    } else {
        // Retoma: empurra o inicio da etapa pelo tempo que ficou pausado
        if (valid && rampPausedAtUnix && rampStepStartUnix) {
            rampStepStartUnix += (unsigned long)now - rampPausedAtUnix;
        }
        isRampPaused = false;
        rampPausedAtUnix = 0;
    }
    save_ramp_state();
    request_dashboard_refresh = true;
    Serial.printf_P(PSTR("[Rampas] Perfil %s\n"), isRampPaused ? "pausado" : "retomado");
    return true;
}

void ramp_stop() {
    isRampActive = false;
    isRampPaused = false;
    rampPausedAtUnix = 0;
    rampPendingElapsedSec = -1;
    save_ramp_state();
    request_dashboard_refresh = true;
    Serial.println(F("[Rampas] Perfil encerrado remotamente"));
}

/** Ao voltar para o modo fermentador com o perfil ativo, retoma a temperatura da etapa */
void ramp_on_mode_change() {
    if (isRampActive) apply_step_setpoint();
}
