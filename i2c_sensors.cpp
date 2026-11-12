// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "config.h"
#include "i2c_sensors.h"

#if BREWW_HAS_LVGL
// Placa com tela grande: o I2C e o do touch; sem sensores I2C por enquanto
void i2c_sensors_begin() {}
void i2c_sensors_loop() {}
const char* i2c_humidity_sensor() { return nullptr; }
bool i2c_humidity_active() { return false; }
#else

#include "globals.h"
#include "board_pins.h"
#include <Wire.h>

enum Kind { NONE, SHT3X, AHT20, BME280 };
static Kind kind = NONE;
static uint8_t addr = 0;
static unsigned long nextAt = 0;
static bool measuring = false;
static uint8_t failCount = 0;

static bool present(uint8_t a) { Wire.beginTransmission(a); return Wire.endTransmission() == 0; }
static bool write_bytes(const uint8_t* b, size_t n) { Wire.beginTransmission(addr); Wire.write(b, n); return Wire.endTransmission() == 0; }
static bool read_bytes(uint8_t* b, size_t n) {
    if (Wire.requestFrom((int)addr, (int)n) != (int)n) return false;
    for (size_t i = 0; i < n; i++) b[i] = Wire.read();
    return true;
}
static bool read_reg(uint8_t reg, uint8_t* b, size_t n) {
    Wire.beginTransmission(addr); Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    return read_bytes(b, n);
}
static bool write_reg(uint8_t reg, uint8_t v) { uint8_t b[2] = {reg, v}; return write_bytes(b, 2); }

// CRC-8 (polinomio 0x31, inicio 0xFF) usado pelo SHT3x e AHT20
static uint8_t crc8(const uint8_t* d, size_t n) {
    uint8_t crc = 0xFF;
    for (size_t i = 0; i < n; i++) { crc ^= d[i]; for (int b = 0; b < 8; b++) crc = (crc & 0x80) ? (crc << 1) ^ 0x31 : crc << 1; }
    return crc;
}

// ---- BME280: calibracao e compensacao (formulas inteiras do datasheet da Bosch) ----
static uint16_t dig_T1; static int16_t dig_T2, dig_T3;
static uint8_t dig_H1, dig_H3; static int16_t dig_H2, dig_H4, dig_H5; static int8_t dig_H6;

static bool bme_init() {
    uint8_t id;
    if (!read_reg(0xD0, &id, 1) || id != 0x60) return false; // 0x58 = BMP280 (sem umidade)
    uint8_t c[26], h[7];
    if (!read_reg(0x88, c, 26) || !read_reg(0xE1, h, 7)) return false;
    dig_T1 = c[0] | (c[1] << 8); dig_T2 = (int16_t)(c[2] | (c[3] << 8)); dig_T3 = (int16_t)(c[4] | (c[5] << 8));
    dig_H1 = c[25];
    dig_H2 = (int16_t)(h[0] | (h[1] << 8)); dig_H3 = h[2];
    // E4 e E6 sao bytes com sinal (datasheet BME280, tabela 16)
    dig_H4 = (int16_t)(((int16_t)(int8_t)h[3] << 4) | (h[4] & 0x0F));
    dig_H5 = (int16_t)(((int16_t)(int8_t)h[5] << 4) | (h[4] >> 4));
    dig_H6 = (int8_t)h[6];
    // umidade x1, temperatura x1, pressao x1, modo normal, espera 1000 ms
    return write_reg(0xF2, 0x01) && write_reg(0xF5, 0xA0) && write_reg(0xF4, 0x27);
}

static bool bme_read(float& hum) {
    uint8_t d[8];
    if (!read_reg(0xF7, d, 8)) return false;
    int32_t adc_T = ((int32_t)d[3] << 12) | ((int32_t)d[4] << 4) | (d[5] >> 4);
    int32_t adc_H = ((int32_t)d[6] << 8) | d[7];
    if (adc_H == 0x8000) return false; // sem leitura ainda
    int32_t v1 = ((((adc_T >> 3) - ((int32_t)dig_T1 << 1))) * (int32_t)dig_T2) >> 11;
    int32_t v2 = (((((adc_T >> 4) - (int32_t)dig_T1) * ((adc_T >> 4) - (int32_t)dig_T1)) >> 12) * (int32_t)dig_T3) >> 14;
    int32_t t_fine = v1 + v2;
    int32_t x = t_fine - 76800;
    x = (((((adc_H << 14) - ((int32_t)dig_H4 << 20) - ((int32_t)dig_H5 * x)) + 16384) >> 15) *
         (((((((x * (int32_t)dig_H6) >> 10) * (((x * (int32_t)dig_H3) >> 11) + 32768)) >> 10) + 2097152) * (int32_t)dig_H2 + 8192) >> 14));
    x = x - (((((x >> 15) * (x >> 15)) >> 7) * (int32_t)dig_H1) >> 4);
    x = x < 0 ? 0 : (x > 419430400 ? 419430400 : x);
    hum = (float)(x >> 12) / 1024.0f;
    return true;
}

// ---- API ----
void i2c_sensors_begin() {
    if (g_pins.sda < 0 || g_pins.scl < 0) return;
    Wire.begin(g_pins.sda, g_pins.scl); // ja iniciado pela tela pequena: nao faz nada
    if (present(0x44) || present(0x45)) { kind = SHT3X; addr = present(0x44) ? 0x44 : 0x45; }
    else if (present(0x38)) { kind = AHT20; addr = 0x38; }
    else if (present(0x76) || present(0x77)) {
        addr = present(0x76) ? 0x76 : 0x77;
        kind = BME280;
        if (!bme_init()) { kind = NONE; Serial.printf("[I2C] 0x%02X nao e um BME280 (BMP280 nao mede umidade)\n", addr); }
    }
    if (kind == AHT20) {
        delay(40);
        uint8_t st = 0;
        read_bytes(&st, 1);
        if (!(st & 0x08)) { const uint8_t initCmd[3] = {0xBE, 0x08, 0x00}; write_bytes(initCmd, 3); delay(10); }
    }
    if (kind != NONE) Serial.printf("[I2C] Sensor de umidade %s em 0x%02X (substitui o DHT22)\n", i2c_humidity_sensor(), addr);
    nextAt = millis() + 1000;
}

const char* i2c_humidity_sensor() {
    return kind == SHT3X ? "sht3x" : kind == AHT20 ? "aht20" : kind == BME280 ? "bme280" : nullptr;
}
bool i2c_humidity_active() { return kind != NONE; }

void i2c_sensors_loop() {
    if (kind == NONE || (long)(millis() - nextAt) < 0) return;
    float hum = NAN;
    bool ok = false;
    if (kind == BME280) {
        ok = bme_read(hum);
        nextAt = millis() + 5000;
    } else if (!measuring) {
        // Dispara a medicao e volta 100 ms depois (sem travar o loop)
        static const uint8_t shtCmd[2] = {0x24, 0x00}; // single shot, alta repetibilidade, sem clock stretching
        static const uint8_t ahtCmd[3] = {0xAC, 0x33, 0x00};
        ok = kind == SHT3X ? write_bytes(shtCmd, 2) : write_bytes(ahtCmd, 3);
        measuring = ok;
        nextAt = millis() + (ok ? 100 : 5000);
        if (ok) return;
    } else {
        measuring = false;
        nextAt = millis() + 5000;
        if (kind == SHT3X) {
            uint8_t d[6];
            if (read_bytes(d, 6) && crc8(d + 3, 2) == d[5]) { hum = 100.0f * ((d[3] << 8) | d[4]) / 65535.0f; ok = true; }
        } else {
            uint8_t d[7];
            if (read_bytes(d, 7) && !(d[0] & 0x80) && crc8(d, 6) == d[6]) {
                uint32_t rh = ((uint32_t)d[1] << 12) | ((uint32_t)d[2] << 4) | (d[3] >> 4);
                hum = rh * 100.0f / 1048576.0f; ok = true;
            }
        }
    }
    if (ok && !isnan(hum)) {
        currentHumidity = constrain(hum, 0.0f, 100.0f);
        failCount = 0;
    } else if (++failCount >= 3) {
        currentHumidity = NAN; // nunca mostrar valor velho
    }
}

#endif // BREWW_HAS_LVGL
