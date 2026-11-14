// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// Camada de plataforma: o mesmo firmware compila no ESP32 (S3, C3, classico) e no ESP8266 (versao Lite).
// Tudo o que muda de um chip para o outro passa por aqui: Wi-Fi, servidor web, HTTP, configuracoes
// gravadas (Preferences), memoria, motivo do reinicio, numeros aleatorios, HMAC e watchdog.
#pragma once
#include <Arduino.h>

#if defined(ESP8266)
    #include <ESP8266WiFi.h>
    #include <ESP8266WebServer.h>
    #include <ESP8266HTTPClient.h>
    #include <WiFiClientSecure.h> // BearSSL
    #include <bearssl/bearssl_hmac.h>
    #include <user_interface.h>
    #include "prefs_lfs.h"        // Preferences sobre LittleFS (o ESP8266 nao tem NVS)
    typedef ESP8266WebServer WebServer;
    #ifndef WIFI_AUTH_OPEN
    #define WIFI_AUTH_OPEN ENC_TYPE_NONE
    #endif
    #ifndef WIFI_SCAN_RUNNING
    #define WIFI_SCAN_RUNNING (-1)
    #endif
#else
    #include <WiFi.h>
    #include <WebServer.h>
    #include <HTTPClient.h>
    #include <WiFiClientSecure.h> // mbedTLS
    #include <Preferences.h>
    #include <esp_heap_caps.h>
    #include <esp_system.h>
    #include <esp_random.h>
    #include <esp_task_wdt.h>
    #include <esp_wifi.h>
    #include "mbedtls/md.h"
#endif

// RAM livre e maior bloco continuo (o TLS precisa de um bloco grande)
inline uint32_t plat_free_heap() {
#if defined(ESP8266)
    return ESP.getFreeHeap();
#else
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
}
inline uint32_t plat_largest_block() {
#if defined(ESP8266)
    return ESP.getMaxFreeBlockSize();
#else
    return heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
}

// PSRAM livre (0 sem PSRAM e no ESP8266)
inline uint32_t plat_psram_free() {
#if defined(ESP8266)
    return 0;
#else
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
#endif
}

// Watchdog: chamar em lacos longos (gravacao da OTA)
inline void plat_wdt_feed() {
#if defined(ESP8266)
    ESP.wdtFeed();
    yield();
#else
    esp_task_wdt_reset();
#endif
}

// Motivo do ultimo reinicio, no mesmo vocabulario do diagnostico remoto (evento "boot")
inline const char* plat_reset_reason() {
#if defined(ESP8266)
    const rst_info* r = ESP.getResetInfoPtr();
    switch (r ? r->reason : 255) {
        case REASON_DEFAULT_RST: return "energia";
        case REASON_SOFT_RESTART: return "software";
        case REASON_EXCEPTION_RST: return "erro_fatal";
        case REASON_SOFT_WDT_RST: return "watchdog_tarefa";
        case REASON_WDT_RST: return "watchdog";
        case REASON_DEEP_SLEEP_AWAKE: return "deep_sleep";
        case REASON_EXT_SYS_RST: return "externo";
        default: return "desconhecido";
    }
#else
    switch (esp_reset_reason()) {
        case ESP_RST_POWERON: return "energia";
        case ESP_RST_SW: return "software";
        case ESP_RST_PANIC: return "erro_fatal";
        case ESP_RST_INT_WDT: return "watchdog_interrupcao";
        case ESP_RST_TASK_WDT: return "watchdog_tarefa";
        case ESP_RST_WDT: return "watchdog";
        case ESP_RST_BROWNOUT: return "queda_tensao";
        case ESP_RST_DEEPSLEEP: return "deep_sleep";
        case ESP_RST_EXT: return "externo";
        default: return "desconhecido";
    }
#endif
}

// Bytes aleatorios do gerador de hardware (com o radio ligado, a entropia e real nos dois chips)
inline void plat_random(uint8_t* out, size_t n) {
#if defined(ESP8266)
    ESP.random(out, n);
#else
    esp_fill_random(out, n);
#endif
}

// HMAC-SHA256 (credencial MQTT e codigo de confirmacao)
inline void plat_hmac_sha256(const uint8_t* key, size_t keyLen, const uint8_t* msg, size_t msgLen, uint8_t out[32]) {
#if defined(ESP8266)
    br_hmac_key_context kc;
    br_hmac_context ctx;
    br_hmac_key_init(&kc, &br_sha256_vtable, key, keyLen);
    br_hmac_init(&ctx, &kc, 0);
    br_hmac_update(&ctx, msg, msgLen);
    br_hmac_out(&ctx, out);
#else
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&md, key, keyLen);
    mbedtls_md_hmac_update(&md, msg, msgLen);
    mbedtls_md_hmac_finish(&md, out);
    mbedtls_md_free(&md);
#endif
}

// Temperatura interna do chip (diagnostico). O ESP8266 nao tem sensor: NAN.
inline float plat_chip_temp() {
#if defined(ESP8266)
    return NAN;
#else
    return temperatureRead();
#endif
}

// Familia do chip para o instalador (Improv)
inline const char* plat_chip_family() {
#if defined(ESP8266)
    return "ESP8266";
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
    return "ESP32-S3";
#elif defined(CONFIG_IDF_TARGET_ESP32C3)
    return "ESP32-C3";
#else
    return "ESP32";
#endif
}

// Operacao longa sem pausa (negociacao TLS): no ESP8266 a conta da chave leva 1-3 s sem ceder o
// processador e o vigia de software (~3,2 s) reiniciava a placa no meio (placa com OLED, 2.10.6). Suspende
// so o vigia de SOFTWARE; o de hardware (~8 s) continua protegendo contra travamento de verdade.
inline void plat_long_op_begin() {
#if defined(ESP8266)
    ESP.wdtFeed();
    ESP.wdtDisable();
#endif
}
inline void plat_long_op_end() {
#if defined(ESP8266)
    ESP.wdtEnable(0);
    ESP.wdtFeed();
#endif
}

// Radio sempre acordado (menos atraso no MQTT)
inline void plat_wifi_no_sleep() {
#if defined(ESP8266)
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
#else
    WiFi.setSleep(false);
#endif
}
