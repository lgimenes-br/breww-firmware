// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include <Arduino.h>
#include "platform.h"
#include <ArduinoJson.h>
#include <PubSubClient.h>
extern PubSubClient mqttClient;
#include "config.h"
#include "globals.h"

void init_network();
void handle_network();
void mqtt_publish_telemetry();
// Acao feita no proprio aparelho (ex.: {"action":"start_batch",...}) para o servidor registrar
void mqtt_publish_device_action(const char* json);
// Diagnostico remoto: enfileira um evento JSON (ex: {"type":"lag","ms":...}) para o
// topico brewbrother/<ID>/diag; enviado assim que houver conexao MQTT.
void diag_report(const char* json);

// Funcoes do Wizard
bool load_wifi_credentials();
void save_wifi_credentials(String ssid, String pass);
void clear_wifi_credentials();
extern String savedSSID;
bool has_wifi_credentials();
void start_wifi_scan();
int check_wifi_scan(); // Retorna o numero de redes achadas ou -1 se carregando
String get_scanned_ssid(int i);
void attempt_wifi_connection(String ssid, String pass);

#endif // NET_MANAGER_H
