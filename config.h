// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef CONFIG_H
#define CONFIG_H

// ==========================================================
// 1. PLACA (pinos e tela)
// ==========================================================
// Escolhida na compilacao com -DBREWW_BOARD_<NOME> (o release compila uma por placa).
// Sem flag (ex.: Arduino IDE) = placa oficial com tela de 3,5".
#if defined(BREWW_BOARD_ESP32_DEVKIT)
    #include "boards/board_esp32_devkit.h"
#elif defined(BREWW_BOARD_S3_HEADLESS)
    #include "boards/board_s3_headless.h"
#elif defined(BREWW_BOARD_C3_HEADLESS)
    #include "boards/board_c3_headless.h"
#else
    #define BREWW_BOARD_S3_35
    #include "boards/board_s3_35.h"
#endif

#define DHTTYPE DHT22

// ==========================================================
// 3. CONFIGURACOES GERAIS DA CERVEJARIA
// ==========================================================
#define WDT_TIMEOUT 10             // Watchdog timeout em segundos
#define DEFAULT_SETPOINT_FERM 18.0 // Temperatura padrao Fermentacao
#define DEFAULT_SETPOINT_CHOPP 2.0 // Temperatura padrao Chopp
#define DEFAULT_HISTERESE 0.5      // Histerese padrao
#define COMPRESSOR_DELAY_SEC 300   // Atraso de protecao do compressor (5 minutos)

// ==========================================================
// 4. MQTT E REDE
// ==========================================================
#define MQTT_SERVER "breww.live"
#define MQTT_TLS_PORT 8883  // MQTT com TLS (certificado Let's Encrypt do breww.live)
// (credencial MQTT compartilhada removida: cada aparelho tem a propria, criada no pareamento)

#endif // CONFIG_H
