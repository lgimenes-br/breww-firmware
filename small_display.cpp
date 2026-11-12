// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "config.h"
#if !BREWW_HAS_LVGL // so nas placas sem tela grande

#include "small_display.h"
#include "board_pins.h"
#include <Wire.h>
#include <U8g2lib.h>
#include <Preferences.h>

// ---------------------------------------------------------------------------
// Deteccao
// ---------------------------------------------------------------------------
static const uint8_t OLED_ADDRS[] = {0x3C, 0x3D};
static const uint8_t LCD_ADDRS[] = {0x27, 0x3F}; // PCF8574 / PCF8574A mais comuns

enum Kind { KIND_NONE, KIND_OLED, KIND_LCD };
static Kind kind = KIND_NONE;
static uint8_t addr = 0;
static U8G2* oled = nullptr;

static bool i2c_present(uint8_t a) {
    Wire.beginTransmission(a);
    return Wire.endTransmission() == 0;
}

// ---------------------------------------------------------------------------
// LCD 20x4 HD44780 via PCF8574 (P0=RS P1=RW P2=EN P3=luz P4..P7=D4..D7)
// ---------------------------------------------------------------------------
static const uint8_t LCD_RS = 0x01, LCD_EN = 0x04, LCD_BL = 0x08;
static char lcdShadow[4][21]; // o que ja esta na tela: so reescreve a linha que mudou

static void lcd_expander(uint8_t v) {
    Wire.beginTransmission(addr);
    Wire.write(v | LCD_BL);
    Wire.endTransmission();
}
static void lcd_nibble(uint8_t v) {
    lcd_expander(v);
    lcd_expander(v | LCD_EN); delayMicroseconds(1);
    lcd_expander(v & ~LCD_EN); delayMicroseconds(50);
}
static void lcd_send(uint8_t value, uint8_t mode) {
    lcd_nibble((value & 0xF0) | mode);
    lcd_nibble(((value << 4) & 0xF0) | mode);
}
static void lcd_cmd(uint8_t c) { lcd_send(c, 0); if (c <= 0x02) delay(2); }

static void lcd_init() {
    delay(50);
    lcd_nibble(0x30); delay(5);
    lcd_nibble(0x30); delayMicroseconds(150);
    lcd_nibble(0x30);
    lcd_nibble(0x20);        // modo 4 bits
    lcd_cmd(0x28);           // 2+ linhas, fonte 5x8
    lcd_cmd(0x0C);           // tela ligada, sem cursor
    lcd_cmd(0x06);           // escreve da esquerda para a direita
    lcd_cmd(0x01);           // limpa
    memset(lcdShadow, 0, sizeof(lcdShadow));
}

static void lcd_line(int row, const char* text) {
    char buf[21];
    snprintf(buf, sizeof(buf), "%-20s", text);
    if (strcmp(buf, lcdShadow[row]) == 0) return;
    static const uint8_t rowAddr[4] = {0x00, 0x40, 0x14, 0x54};
    lcd_cmd(0x80 | rowAddr[row]);
    for (int i = 0; i < 20; i++) lcd_send((uint8_t)buf[i], LCD_RS);
    memcpy(lcdShadow[row], buf, sizeof(buf));
}

// O LCD nao tem acentos: troca os mais comuns e usa o grau do proprio LCD (0xDF)
static void ascii_fold(const char* in, char* out, size_t n) {
    size_t o = 0;
    for (const unsigned char* p = (const unsigned char*)in; *p && o + 1 < n; p++) {
        if (*p == 0xC3 && p[1]) {
            unsigned char c = *++p;
            const char* map = "AAAAAAACEEEEIIII" "DNOOOOOxOUUUUYPs" "aaaaaaaceeeeiiii" "dnooooo/ouuuuypy";
            out[o++] = (c >= 0x80 && c <= 0xBF) ? map[c - 0x80] : '?';
        } else if (*p == 0xC2 && p[1] == 0xB0) { p++; out[o++] = (char)0xDF; }
        else if (*p < 0x80) out[o++] = (char)*p;
    }
    out[o] = 0;
}

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------
const char* small_display_begin() {
    if (g_pins.sda < 0 || g_pins.scl < 0) { Serial.println("[Tela] I2C desligado nos pinos"); return nullptr; }
    Wire.begin(g_pins.sda, g_pins.scl);
    Wire.setClock(400000);
    for (uint8_t a : OLED_ADDRS) if (i2c_present(a)) { kind = KIND_OLED; addr = a; break; }
    if (kind == KIND_NONE) for (uint8_t a : LCD_ADDRS) if (i2c_present(a)) { kind = KIND_LCD; addr = a; break; }

    if (kind == KIND_OLED) {
        Preferences p;
        p.begin("breww", true);
        String drv = p.getString("oled_drv", "ssd1306");
        p.end();
        if (drv == "sh1106") oled = new U8G2_SH1106_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
        else oled = new U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
        oled->setI2CAddress(addr << 1);
        oled->begin();
        oled->enableUTF8Print();
        Serial.printf("[Tela] OLED encontrado em 0x%02X (driver %s; mude com: tela sh1106 | tela ssd1306)\n", addr, drv.c_str());
        return "oled";
    }
    if (kind == KIND_LCD) {
        lcd_init();
        Serial.printf("[Tela] LCD 20x4 encontrado em 0x%02X\n", addr);
        return "lcd2004";
    }
    Serial.println("[Tela] Nenhuma tela I2C encontrada (opcional)");
    return nullptr;
}

const char* small_display_kind() {
    return kind == KIND_OLED ? "oled" : (kind == KIND_LCD ? "lcd2004" : nullptr);
}

bool small_display_set_oled_driver(const String& driver) {
    if (driver != "ssd1306" && driver != "sh1106") return false;
    Preferences p;
    p.begin("breww", false);
    p.putString("oled_drv", driver);
    p.end();
    return true;
}

static void draw_oled(const SmallDisplayState& s) {
    char buf[48];
    oled->clearBuffer();
    oled->setFont(u8g2_font_6x12_tf);

    if (s.otaActive) {
        oled->drawUTF8(0, 12, "Atualizando firmware");
        oled->drawFrame(0, 28, 128, 12);
        oled->drawBox(2, 30, (124 * constrain(s.otaPercent, 0, 100)) / 100, 8);
        snprintf(buf, sizeof(buf), "%d%%  nao desligue", s.otaPercent);
        oled->drawUTF8(0, 56, buf);
        oled->sendBuffer();
        return;
    }
    if (s.menuTitle) {
        oled->drawUTF8(0, 10, s.menuTitle);
        oled->drawHLine(0, 13, 128);
        oled->setFont(u8g2_font_helvB14_tf);
        const char* v = s.menuValue ? s.menuValue : "";
        oled->drawUTF8((128 - oled->getUTF8Width(v)) / 2, 40, v);
        oled->setFont(u8g2_font_6x12_tf);
        if (s.menuHint) oled->drawUTF8((128 - oled->getUTF8Width(s.menuHint)) / 2, 62, s.menuHint);
        oled->sendBuffer();
        return;
    }
    if (s.pairPending) {
        oled->drawUTF8(0, 12, "Pedido de vinculo");
        oled->drawUTF8(0, 28, s.pairEmail ? s.pairEmail : "");
        oled->drawUTF8(0, 46, "Segure o BOOT por 3s");
        oled->drawUTF8(0, 60, "para confirmar");
        oled->sendBuffer();
        return;
    }

    // Cabecalho: modo e conexao
    oled->drawUTF8(0, 10, s.modeName);
    const char* net = s.cloud ? "NUVEM" : (s.wifi ? "WI-FI" : "OFFLINE");
    oled->drawUTF8(128 - oled->getUTF8Width(net), 10, net);
    oled->drawHLine(0, 13, 128);

    // Temperatura grande
    oled->setFont(u8g2_font_logisoso24_tf);
    if (isnan(s.temp) || s.temp < -100) snprintf(buf, sizeof(buf), "--.-");
    else snprintf(buf, sizeof(buf), "%.1f", s.temp);
    oled->drawUTF8(0, 42, buf);
    int w = oled->getUTF8Width(buf);
    oled->setFont(u8g2_font_6x12_tf);
    oled->drawUTF8(w + 2, 26, "°C");

    // Alvo e rele
    snprintf(buf, sizeof(buf), "Alvo %.1f°C", s.setpoint);
    oled->drawUTF8(0, 54, buf);
    oled->drawUTF8(128 - oled->getUTF8Width(s.relay), 54, s.relay);

    // Rodape: codigo de vinculo tem prioridade (aparelho ainda nao vinculado)
    if (s.pairCode) snprintf(buf, sizeof(buf), "Codigo: %s", s.pairCode);
    else snprintf(buf, sizeof(buf), "%s", s.footer ? s.footer : "");
    oled->drawUTF8(0, 64, buf);
    oled->sendBuffer();
}

static void draw_lcd(const SmallDisplayState& s) {
    char raw[48], line[24];
    if (s.otaActive) {
        lcd_line(0, "Atualizando...");
        int bars = (20 * constrain(s.otaPercent, 0, 100)) / 100;
        char bar[21]; memset(bar, ' ', 20); memset(bar, 0xFF, bars); bar[20] = 0;
        lcd_line(1, bar);
        snprintf(raw, sizeof(raw), "%d%%", s.otaPercent); lcd_line(2, raw);
        lcd_line(3, "Nao desligue");
        return;
    }
    if (s.menuTitle) {
        char line[24];
        ascii_fold(s.menuTitle, line, sizeof(line)); lcd_line(0, line);
        ascii_fold(s.menuValue ? s.menuValue : "", line, sizeof(line));
        char centered[24]; int pad = (20 - (int)strlen(line)) / 2; if (pad < 0) pad = 0;
        snprintf(centered, sizeof(centered), "%*s%s", pad, "", line); lcd_line(1, centered);
        lcd_line(2, "");
        ascii_fold(s.menuHint ? s.menuHint : "", line, sizeof(line)); lcd_line(3, line);
        return;
    }
    if (s.pairPending) {
        lcd_line(0, "Pedido de vinculo");
        ascii_fold(s.pairEmail ? s.pairEmail : "", line, sizeof(line)); lcd_line(1, line);
        lcd_line(2, "Segure o BOOT 3s");
        lcd_line(3, "para confirmar");
        return;
    }
    const char* net = s.cloud ? "NUVEM" : (s.wifi ? "WIFI" : "OFF");
    ascii_fold(s.modeName, line, sizeof(line));
    snprintf(raw, sizeof(raw), "%-14s%6s", line, net); lcd_line(0, raw);
    if (isnan(s.temp) || s.temp < -100) snprintf(raw, sizeof(raw), "Temp: --.-%cC", 0xDF);
    else snprintf(raw, sizeof(raw), "Temp: %.1f%cC", s.temp, 0xDF);
    lcd_line(1, raw);
    snprintf(raw, sizeof(raw), "Alvo: %.1f%cC %.6s", s.setpoint, 0xDF, s.relay); lcd_line(2, raw);
    if (s.pairCode) snprintf(raw, sizeof(raw), "Codigo: %s", s.pairCode);
    else snprintf(raw, sizeof(raw), "%s", s.footer ? s.footer : "");
    ascii_fold(raw, line, sizeof(line)); lcd_line(3, line);
}

void small_display_update(const SmallDisplayState& s) {
    if (kind == KIND_NONE) return;
    static unsigned long last = 0;
    unsigned long interval = s.otaActive ? 250 : (s.menuTitle ? 100 : 500); // menu responde rapido
    if (millis() - last < interval) return;
    last = millis();
    if (kind == KIND_OLED) draw_oled(s);
    else draw_lcd(s);
}

#endif // !BREWW_HAS_LVGL
