// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "config.h"
#if !BREWW_HAS_LVGL

#include "button_menu.h"
#include "board_pins.h"
#include "globals.h"
#include "device_settings.h"
#include "hal_profiles.h"
#include "net_manager.h"

static const unsigned long DEBOUNCE_MS = 30;
static const unsigned long REPEAT_DELAY_MS = 600;
static const unsigned long REPEAT_EVERY_MS = 150;
static const unsigned long SAVE_TARGET_MS = 3000;
static const unsigned long MENU_TIMEOUT_MS = 10000;

enum Ev { EV_NONE, EV_UP, EV_DOWN, EV_BOTH };
enum State { IDLE, EDIT_TARGET, MENU, EDIT_MODE };
static const char* MENU_ITEMS[] = {"Modo", "Alvo", "Perfil", "Sair"};
static const int MENU_COUNT = 4;
static const char* MODE_NAMES[] = {"Fermentador", "Geladeira", "Choppeira", "Limpeza"};

static bool enabled = false;
static State state = IDLE;
static int menuIndex = 0;
static int modeChoice = 0;
static float targetDraft = 0;
static unsigned long lastActivity = 0;
static char valueBuf[24], hintBuf[40];

// ---- Leitura dos botoes (ativos em nivel baixo, com pull-up) ----
struct Btn { int8_t pin; bool stable; bool raw; unsigned long changedAt; unsigned long pressedAt; unsigned long lastRepeat; bool consumed; };
static Btn up = {-1}, down = {-1};
static bool bothSeen = false;

static void btn_sample(Btn& b) {
    bool r = digitalRead(b.pin) == LOW;
    if (r != b.raw) { b.raw = r; b.changedAt = millis(); }
    if (millis() - b.changedAt >= DEBOUNCE_MS && b.stable != b.raw) {
        b.stable = b.raw;
        if (b.stable) { b.pressedAt = millis(); b.lastRepeat = 0; b.consumed = false; }
    }
}

static Ev read_event() {
    bool wasUp = up.stable, wasDown = down.stable;
    btn_sample(up); btn_sample(down);
    if (up.stable && down.stable) { bothSeen = true; return EV_NONE; }
    if (bothSeen) {
        if (!up.stable && !down.stable) { bothSeen = false; return EV_BOTH; } // soltou os dois
        return EV_NONE;
    }
    // Segurando um botao so: repete (para mudar o alvo depressa)
    for (Btn* b : {&up, &down}) {
        if (b->stable && millis() - b->pressedAt > REPEAT_DELAY_MS && millis() - b->lastRepeat > REPEAT_EVERY_MS) {
            b->lastRepeat = millis(); b->consumed = true;
            return b == &up ? EV_UP : EV_DOWN;
        }
    }
    // Toque curto: dispara ao soltar (assim "os dois juntos" nao vira um toque)
    if (wasUp && !up.stable && !up.consumed) return EV_UP;
    if (wasDown && !down.stable && !down.consumed) return EV_DOWN;
    return EV_NONE;
}

// ---- Acoes ----
static void commit_target() {
    if (fabsf(targetDraft - currentSetpoint) > 0.01f) settings_set_setpoint(targetDraft);
}

void menu_begin() {
    up.pin = g_pins.btnUp; down.pin = g_pins.btnDown;
    enabled = up.pin >= 0 && down.pin >= 0;
    if (!enabled) return;
    pinMode(up.pin, INPUT_PULLUP);
    pinMode(down.pin, INPUT_PULLUP);
    Serial.printf_P(PSTR("[Menu] Botoes nos GPIO %d (subir) e %d (descer). Os dois juntos abrem o menu.\n"), up.pin, down.pin);
}

bool menu_active() { return enabled && state != IDLE; }

void menu_loop() {
    if (!enabled) return;
    Ev ev = read_event();
    unsigned long now = millis();
    if (ev != EV_NONE) lastActivity = now;

    switch (state) {
    case IDLE:
        if (ev == EV_UP || ev == EV_DOWN) {
            targetDraft = currentSetpoint + (ev == EV_UP ? 0.5f : -0.5f);
            state = EDIT_TARGET;
        } else if (ev == EV_BOTH) { state = MENU; menuIndex = 0; }
        break;
    case EDIT_TARGET:
        if (ev == EV_UP) targetDraft += 0.5f;
        else if (ev == EV_DOWN) targetDraft -= 0.5f;
        else if (ev == EV_BOTH || now - lastActivity > SAVE_TARGET_MS) { commit_target(); state = IDLE; }
        targetDraft = constrain(targetDraft, -10.0f, 40.0f);
        break;
    case MENU:
        if (ev == EV_UP) menuIndex = (menuIndex + MENU_COUNT - 1) % MENU_COUNT;
        else if (ev == EV_DOWN) menuIndex = (menuIndex + 1) % MENU_COUNT;
        else if (ev == EV_BOTH) {
            if (menuIndex == 0) { modeChoice = constrain(currentMode, 0, 3); state = EDIT_MODE; }
            else if (menuIndex == 1) { targetDraft = currentSetpoint; state = EDIT_TARGET; }
            else if (menuIndex == 2) { if (isRampActive && ramp_toggle_pause()) mqtt_publish_telemetry(); state = IDLE; }
            else state = IDLE;
        } else if (now - lastActivity > MENU_TIMEOUT_MS) state = IDLE;
        break;
    case EDIT_MODE:
        if (ev == EV_UP) modeChoice = (modeChoice + 3) % 4;
        else if (ev == EV_DOWN) modeChoice = (modeChoice + 1) % 4;
        else if (ev == EV_BOTH) {
            if (modeChoice != currentMode) { settings_set_mode(modeChoice); request_dashboard_refresh = true; }
            state = IDLE;
        } else if (now - lastActivity > MENU_TIMEOUT_MS) state = IDLE;
        break;
    }
}

void menu_fill(SmallDisplayState& st) {
    switch (state) {
    case EDIT_TARGET:
        st.menuTitle = "Alvo";
        snprintf_P(valueBuf, sizeof(valueBuf), PSTR("%.1f°C"), targetDraft);
        st.menuValue = valueBuf;
        st.menuHint = "Grava em 3 s";
        break;
    case MENU:
        st.menuTitle = "Menu";
        if (menuIndex == 2) snprintf_P(valueBuf, sizeof(valueBuf), PSTR("%s"), !isRampActive ? "Perfil: -" : (isRampPaused ? "Retomar" : "Pausar"));
        else snprintf_P(valueBuf, sizeof(valueBuf), PSTR("%s"), MENU_ITEMS[menuIndex]);
        st.menuValue = valueBuf;
        st.menuHint = "Os 2 juntos: escolher";
        break;
    case EDIT_MODE:
        st.menuTitle = "Modo";
        st.menuValue = MODE_NAMES[modeChoice];
        snprintf_P(hintBuf, sizeof(hintBuf), PSTR("%s"), modeChoice == currentMode ? "Atual" : "Os 2 juntos: confirmar");
        st.menuHint = hintBuf;
        break;
    default: break;
    }
}

#endif // !BREWW_HAS_LVGL
