// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#ifndef CLOUD_PAIRING_H
#define CLOUD_PAIRING_H

#include <Arduino.h>

// ID do aparelho = MAC Wi-Fi STA sem ":" (mesmo ID dos topicos MQTT brewbrother/<ID>/...)
String get_device_id();

// Raizes ISRG (Let's Encrypt) para validar breww.live (HTTPS e MQTT/TLS)
const char* cloud_root_cas();

// Chamar no loop sempre que o Wi-Fi estiver conectado.
// Anuncia o aparelho ao servidor (HTTPS) e entrega pedidos de vinculo para a tela.
void cloud_pairing_loop();

// MQTT "pair_request": consulta o servidor agora (o pedido em si vem por HTTPS)
void cloud_pairing_request_check();

// Codigo de confirmacao (8 caracteres) que prova que e este aparelho no pareamento:
// HMAC-SHA256(chave, "breww-pair-v1"), 8 primeiros hex maiusculos (mesmo calculo de
// backend/routes/pairingRoutes.js). Tambem e a senha da pagina local /update.
// Retorna false enquanto a chave ainda nao foi gerada (ela nasce com o Wi-Fi ligado).
bool cloud_pair_code(String& out);

// Credencial MQTT propria do aparelho (usuario = ID, senha = HMAC da chave).
// Retorna false enquanto o servidor ainda nao vinculou a chave (o aparelho so entra no
// broker depois de pareado, com confirmacao fisica).
bool cloud_mqtt_credentials(String& user, String& pass);
// Chamado quando o broker recusa a credencial propria (ex: servidor perdeu o registro)
void cloud_mqtt_credentials_rejected();

// Atualizacao OTA segura: so aceita URLs https://breww.live/, valida o certificado
// (raizes ISRG) e o servidor manda o MD5 no header x-MD5, conferido antes de ativar.
// Retorna true se o firmware novo foi gravado (o chamador reinicia).
bool cloud_ota_update(const String& url, void (*onProgress)(int current, int total), String* errorOut = nullptr);
// Pausa o HTTPS de fundo (anuncio/pareamento) durante a OTA
void cloud_pause_background(bool paused);

// Resposta do usuario na tela (Confirmar / Recusar). Processada no proximo loop.
void cloud_pairing_respond(bool accept);

#endif // CLOUD_PAIRING_H
