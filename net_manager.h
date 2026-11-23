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

// Conexao com a rede: Wi-Fi ou, nas placas com RJ45 (BREWW_HAS_ETH), o cabo. O cabo tem prioridade.
bool mqtt_online();         // MQTT conectado e livre (false enquanto a tarefa de fundo conecta)
bool net_online();          // Wi-Fi conectado ou cabo com IP
bool net_eth_up();          // cabo com IP (sempre false sem RJ45)
String net_local_ip();      // IP da interface em uso ("" sem rede)
// Nome do aparelho na rede local: "breww-" + 4 ultimos caracteres do ID (ex.: breww-b359 -> breww-b359.local).
// Proprio de cada aparelho: com dois Breww na mesma rede, o iSpindel manda para o certo.
String net_hostname();

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
int wifi_rejected_count(); // nos da mesh que recusaram este aparelho (fora da escolha por 6 h)

#endif // NET_MANAGER_H
