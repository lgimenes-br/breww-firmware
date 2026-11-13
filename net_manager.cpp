// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "hal_sensors.h"
#include "ispindel.h"
#include "hal_profiles.h"
#include "net_manager.h"
#include "ui.h"

#include "platform.h"
#if !BREWW_LITE
#include <ArduinoOTA.h> // atualizacao pela IDE na rede local (fora da Lite: RAM)
#endif
#if !defined(ESP8266)
#include <Update.h>
#endif
#include "hal_relays.h"
#include "cloud_pairing.h"
#include "ota_guard.h"
#include "device_settings.h"
#include "wifi_portal.h"
#include "wifi_setup.h"
#include "board_pins.h"
#include "i2c_sensors.h"
#if !BREWW_HAS_LVGL
#include "small_display.h"
#endif
#include "version.h"

WiFiClientSecure espClientTls; // MQTT com TLS (padrao)
PubSubClient mqttClient(espClientTls);
WebServer server(80);
Preferences preferences;

unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastTelemetryPublish = 0;
unsigned long lastTelemetry = 0;

String savedSSID = "";
String savedPASS = "";

extern bool request_dashboard_refresh;

// Status da OTA para o site (topico brewbrother/<ID>/ota, repassado pelo /api/live).
// stage: received | connecting | downloading | verifying | retry | rebooting | failed
// "progress" continua presente para compatibilidade com telas antigas.
// IMPORTANTE: nada de mqttClient.loop() aqui. Com MQTT sobre TLS, o loop() verifica a
// conexao segura e pode esperar o timeout do socket a cada chamada (era ~1 s por
// porcento: download de 1,6 MB levando 100 s). Publicar nao precisa do loop(), e os
// proprios publishes mantem a conexao viva. Progresso limitado a 1 publish / 500 ms.
static void publish_ota_status(const char* stage, int progress, int attempt = 0, const char* error = nullptr) {
    static int lastProgress = -1;
    static String lastStage;
    static unsigned long lastPublishMs = 0;
    bool isDownload = strcmp(stage, "downloading") == 0;
    if (isDownload && lastStage == stage) {
        if (progress == lastProgress || millis() - lastPublishMs < 500) return;
    }
    lastStage = stage;
    lastProgress = progress;
    lastPublishMs = millis();
    if (!mqttClient.connected()) return;
    StaticJsonDocument<256> doc;
    doc["stage"] = stage;
    doc["progress"] = progress;
    if (attempt > 0) doc["attempt"] = attempt;
    if (error && error[0]) doc["error"] = error;
    char payload[256];
    serializeJson(doc, payload, sizeof(payload));
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mqttClient.publish(("brewbrother/" + mac + "/ota").c_str(), payload, false);
}

static bool otaVerifyingSent = false;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
    Serial.printf_P(PSTR("Mensagem recebida [%s]\n"), topic);
    
    // Sem copia na pilha (no ESP8266 ela tem 4 KB e o payload chega a 2 KB)
    Serial.print(F("Payload: "));
    Serial.write(payload, length > 400 ? 400 : length);
    Serial.println(length > 400 ? "..." : "");

    // Ignora comandos destinados a OUTROS aparelhos (a assinatura "brewbrother/+/command"
    // entrega comandos de todos os dispositivos do broker)
    {
        String t(topic);
        int a = t.indexOf('/');
        int b = t.indexOf('/', a + 1);
        String target = (a >= 0 && b > a) ? t.substring(a + 1, b) : "";
        target.replace(":", "");
        target.toUpperCase();
        String myMac = WiFi.macAddress();
        myMac.replace(":", "");
        myMac.toUpperCase();
        if (target.length() && target != myMac && target != "GLOBAL") {
            Serial.printf_P(PSTR("Comando para outro aparelho (%s), ignorado.\n"), target.c_str());
            return;
        }
    }

    // Heap (nao pilha): setProfile com ate 10 etapas nao cabe nos 768 bytes de antes
    DynamicJsonDocument doc(3072);
    DeserializationError error = deserializeJson(doc, payload, length);
    if (error) {
        Serial.print(F("deserializeJson() failed: "));
        Serial.println(error.f_str());
        return;
    }
    
    // Note: the web app payload is: { type: "setChopp", cmd: "setChopp", field: "...", val: "..." }
    const char* cmd = doc["cmd"];
    if (!cmd) {
        cmd = doc["type"]; // Fallback just in case
        if (!cmd) {
            Serial.println(F("Missing 'cmd' or 'type' in JSON"));
            return;
        }
    }
    Serial.printf_P(PSTR("Comando recebido: %s\n"), cmd);
    
    
    if (strcmp(cmd, "update_firmware") == 0) {
        if (doc.containsKey("url")) {
            String url = doc["url"].as<String>();
#if !defined(BREWW_BOARD_S3_35)
            // Um firmware por placa: o servidor escolhe o arquivo pelo ?board= (sem ele, entrega o
            // da placa com tela). O app/site mandam a URL sem placa; quem sabe a placa e o aparelho.
            if (url.indexOf("board=") < 0) {
                url += (url.indexOf('?') < 0) ? "?board=" : "&board=";
                url += BREWW_BOARD_NAME;
            }
#endif
            // Confirma ao site ANTES de qualquer outra coisa (o modal mostra na hora)
            publish_ota_status("received", 0);
            Serial.printf_P(PSTR("[OTA] Iniciando atualizacao: %s\n"), url.c_str());
            isUpdatingOTA = true;
            override_relays(false, false);
            ui_show_ota_screen();
            cloud_pause_background(true); // nada de HTTPS de fundo disputando memoria/rede
#if defined(ESP8266)
            // RAM para uma conexao TLS so: o MQTT sai durante o download (o site deixa de ver o progresso)
            publish_ota_status("downloading", 1);
            mqttClient.disconnect();
#endif
            const int MAX_TENTATIVAS = 3;
            bool ok = false;
            String lastError;
            for (int tentativa = 1; tentativa <= MAX_TENTATIVAS && !ok; tentativa++) {
                if (tentativa > 1) {
                    char msg[64];
                    snprintf_P(msg, sizeof(msg), PSTR("Falha na conexao. Tentativa %d de %d..."), tentativa, MAX_TENTATIVAS);
                    Serial.printf_P(PSTR("[OTA] %s\n"), msg);
                    ui_ota_status(msg);
                    publish_ota_status("retry", 0, tentativa, lastError.c_str());
                    delay(3000);
                }
                publish_ota_status("connecting", 0, tentativa);
                otaVerifyingSent = false;
                ok = cloud_ota_update(url, [](int current, int total) {
                    if (total > 0) {
                        int percent = (int)(((long long)current * 100) / total);
                        ui_update_ota_progress(percent);
                        if (percent >= 100) {
                            // Download completo: Update.end() confere o MD5 antes de ativar
                            if (!otaVerifyingSent) { otaVerifyingSent = true; publish_ota_status("verifying", 100); }
                        } else {
                            publish_ota_status("downloading", percent < 1 ? 1 : percent);
                        }
                    }
                }, &lastError);
            }
            if (ok) publish_ota_status("rebooting", 100);
            else publish_ota_status("failed", 0, MAX_TENTATIVAS, lastError.c_str());
            ui_show_ota_success(ok);
            cloud_pause_background(false);
            isUpdatingOTA = false;
            if (ok) {
                Serial.println(F("[OTA] Reiniciando..."));
                delay(1500);
                ESP.restart();
            }
            delay(2000);
            ui_hide_ota_screen(); // falha definitiva: volta para a tela normal
            request_dashboard_refresh = true;
            return;
        }
    } else if (strcmp(cmd, "setMode") == 0) {
        if (doc.containsKey("value")) {
            unsigned long t0 = millis();
            settings_set_mode(doc["value"].as<int>()); // confirma na nuvem e grava
            request_dashboard_refresh = true; // tela do novo modo e montada no proximo ciclo do loop
            Serial.printf_P(PSTR("[CMD] setMode %d em %lu ms\n"), currentMode, millis() - t0);
        }
    }
    // ---- Perfil de fermentacao (app/site/servidor) ----
    else if (strcmp(cmd, "setProfile") == 0) {
        JsonArrayConst steps = doc["steps"].as<JsonArrayConst>();
        int idx = doc.containsKey("currentStep") ? doc["currentStep"].as<int>() : (doc["currStep"] | 0);
        float stepTime = doc["stepTime"] | 0.0f;
        if (!steps.isNull() && ramp_set_profile(steps, idx, stepTime)) {
            mqtt_publish_telemetry(); // confirma na hora (alvo e etapa novos)
        }
        return;
    }
    else if (strcmp(cmd, "skipStep") == 0) {
        if (ramp_skip_step()) mqtt_publish_telemetry();
        return;
    }
    else if (strcmp(cmd, "togglePause") == 0) {
        if (ramp_toggle_pause()) mqtt_publish_telemetry();
        return;
    }
    else if (strcmp(cmd, "stopProfile") == 0) {
        if (isRampActive) {
            ramp_stop();
            mqtt_publish_telemetry();
        }
        return;
    }
    else if (strcmp(cmd, "setManual") == 0) {
        if (doc.containsKey("field") && doc.containsKey("val")) {
            const char* field = doc["field"];
            if (strcmp(field, "setpoint_manual") == 0 || strcmp(field, "chopp_setpoint") == 0) {
                settings_set_setpoint(doc["val"].as<float>()); // publica na hora; grava com debounce
            }
        }
    }
    else if (strcmp(cmd, "request_status") == 0) {
        extern unsigned long lastTelemetry;
        lastTelemetry = 0; // Force immediate publish
    }
    else if (strcmp(cmd, "pair_request") == 0) {
        // So um aviso: os detalhes do pedido sao buscados por HTTPS autenticado
        cloud_pairing_request_check();
    }
    else if (strcmp(cmd, "scan_sensors") == 0) {
        String macs[5];
        float temps[5];
        int count = scan_ds18b20(macs, temps, 5);

        String mac = WiFi.macAddress();
        mac.replace(":", "");

        StaticJsonDocument<768> resp;
        resp["type"] = "scan_sensors_response";
        resp["id"] = mac;
        resp["macCtrl"] = sensorMacCtrl;
        resp["mac2"] = sensorMac2;
        JsonArray arrMacs = resp.createNestedArray("macs");
        JsonArray arrSensors = resp.createNestedArray("sensors");
        for (int i = 0; i < count; i++) {
            if (macs[i].length() == 0) continue;
            arrMacs.add(macs[i]);
            JsonObject o = arrSensors.createNestedObject();
            o["mac"] = macs[i];
            if (isnan(temps[i])) o["temp"] = nullptr;
            else o["temp"] = roundf(temps[i] * 100.0f) / 100.0f;
        }
        static char out[768]; // fora da pilha (ESP8266: 4 KB)
        serializeJson(resp, out, sizeof(out));
        mqttClient.publish(("brewbrother/" + mac + "/response").c_str(), out, false);
        Serial.printf_P(PSTR("Scan publicado: %s\n"), out);
    }
    else if (strcmp(cmd, "set_sensors") == 0) {
        String ctrl = doc["macSensorControle"] | "";
        String m2 = "";
        if (doc["extraSensors"].is<JsonArray>() && doc["extraSensors"].size() > 0) {
            m2 = doc["extraSensors"][0]["mac"] | "";
        }
        set_sensor_mapping(ctrl, m2);
        extern unsigned long lastTelemetry;
        lastTelemetry = 0; // publica ja com o novo mapeamento
    }
    else if (strcmp(cmd, "set_pins") == 0) {
        // Pinos (placas sem tela): {"ow":23,"dht":19,"h":26,"c":27,"sda":21,"scl":22,"bup":-1,"bdn":-1}
        // Campo ausente = mantem. Valido: grava, desliga os reles e reinicia. Invalido: erro na telemetria.
        BoardPins np = g_pins;
        if (doc["reset"] | false) {
            pins_reset_defaults();
            Serial.println(F("[Pinos] Voltando aos pinos padrao da placa. Reiniciando..."));
            override_relays(false, false); delay(500); ESP.restart();
        }
        if (doc.containsKey("ow")) np.oneWire = doc["ow"].as<int>();
        if (doc.containsKey("dht")) np.dht = doc["dht"].as<int>();
        if (doc.containsKey("h")) np.heater = doc["h"].as<int>();
        if (doc.containsKey("c")) np.cooler = doc["c"].as<int>();
        if (doc.containsKey("sda")) np.sda = doc["sda"].as<int>();
        if (doc.containsKey("scl")) np.scl = doc["scl"].as<int>();
        if (doc.containsKey("bup")) np.btnUp = doc["bup"].as<int>();
        if (doc.containsKey("bdn")) np.btnDown = doc["bdn"].as<int>();
        String err;
        if (pins_save(np, err)) {
            Serial.println(F("[Pinos] Pinos novos gravados. Reiniciando..."));
            override_relays(false, false);
            delay(500);
            ESP.restart();
        }
        Serial.printf_P(PSTR("[Pinos] Recusado: %s\n"), err.c_str());
        pins_set_last_error(err);
        extern unsigned long lastTelemetry;
        lastTelemetry = 0; // o app ve o erro (pinsErr) na proxima telemetria
    }
    else if (strcmp(cmd, "set_relays") == 0) {
        // Polaridade dos reles: {"heaterInverted":true,"coolerInverted":true} (campo ausente = mantem)
        bool h = doc.containsKey("heaterInverted") ? doc["heaterInverted"].as<bool>() : relay_heater_inverted();
        bool c = doc.containsKey("coolerInverted") ? doc["coolerInverted"].as<bool>() : relay_cooler_inverted();
        relays_set_inverted(h, c);
        extern unsigned long lastTelemetry;
        lastTelemetry = 0; // confirma na telemetria (relInv)
    }
    else if (strcmp(cmd, "setChopp") == 0) {
        if (doc.containsKey("field") && doc.containsKey("val")) {
            const char* field = doc["field"];
            
            if (strcmp(field, "c_n1") == 0) {
                settings_set_beer_name(doc["val"].as<String>());
            }
            else if (strcmp(field, "c_n3") == 0) {
                settings_set_beer_type(doc["val"].as<String>());
            }
            else if (strcmp(field, "c_ibu") == 0) {
                settings_set_beer_ibu(doc["val"].as<int>());
            }
            else if (strcmp(field, "c_vol") == 0) {
                settings_set_beer_abv(doc["val"].as<float>());
            }
        }
    }
    
}

// Transporte do MQTT: sempre TLS na 8883 (a 1883 sem criptografia fica so dentro do servidor,
// fechada para a internet). Nao ha mais queda para conexao sem criptografia.

boolean reconnectMqtt() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String clientId = "Breww_" + mac;

    mqttClient.setClient(espClientTls);
    mqttClient.setServer(MQTT_SERVER, MQTT_TLS_PORT);
#if defined(ESP8266)
    cloud_tls_setup(espClientTls, MQTT_TLS_PORT); // hora atual para validar o certificado + buffers (MFLN)
#endif

    // Credencial propria do aparelho: o servidor so a cria depois do pareamento confirmado.
    // Antes disso o aparelho nao entra no broker (o pedido de vinculo chega pelo anuncio HTTPS).
    String user, pass;
    if (!cloud_mqtt_credentials(user, pass)) return false;
#if defined(ESP8266)
    if (time(nullptr) < 1704067200) return false; // BearSSL valida a data do certificado: espera o NTP
#endif

    if (mqttClient.connect(clientId.c_str(), user.c_str(), pass.c_str())) {
        Serial.println(F("MQTT Conectado! (credencial propria, TLS 8883)"));
        // So o proprio topico de comandos. O backend publica cada comando em /command E em
        // /comando (compatibilidade); assinar os dois executava tudo duas vezes (tela
        // reconstruida e flash gravada em dobro a cada troca de modo).
        mqttClient.subscribe(("brewbrother/" + mac + "/command").c_str());
    } else {
        int st = mqttClient.state();
        Serial.printf_P(PSTR("MQTT falhou (state %d, TLS 8883)\n"), st);
        if (st == MQTT_CONNECT_FAILED) {
            char tlsErr[100] = {0};
#if defined(ESP8266)
            espClientTls.getLastSSLError(tlsErr, sizeof(tlsErr));
#else
            espClientTls.lastError(tlsErr, sizeof(tlsErr));
#endif
            Serial.printf_P(PSTR("[MQTT] TLS: %s\n"), tlsErr);
        }
        // 4 = usuario/senha invalidos, 5 = nao autorizado
        if ((st == MQTT_CONNECT_BAD_CREDENTIALS || st == MQTT_CONNECT_UNAUTHORIZED)) {
            cloud_mqtt_credentials_rejected();
        }
    }
    return mqttClient.connected();
}

bool load_wifi_credentials() {
    preferences.begin("breww", true); // RO
    savedSSID = preferences.getString("ssid", "");
    savedPASS = preferences.getString("pass", "");
    preferences.end();
    return savedSSID.length() > 0;
}

void save_wifi_credentials(String ssid, String pass) {
    preferences.begin("breww", false); // RW
    preferences.putString("ssid", ssid);
    preferences.putString("pass", pass);
    preferences.end();
    savedSSID = ssid;
    savedPASS = pass;
}

void clear_wifi_credentials() {
    preferences.begin("breww", false);
    preferences.remove("ssid");
    preferences.remove("pass");
    preferences.end();
    savedSSID = "";
    savedPASS = "";
}

void attempt_wifi_connection(String ssid, String pass) {
    Serial.printf_P(PSTR("Tentando conectar a: %s\n"), ssid.c_str());
    WiFi.disconnect();
    plat_wifi_no_sleep(); // Evita que o modem WiFi hiberne e cause lag no MQTT
#if !defined(ESP8266)
    // Rede mesh: o padrao (WIFI_FAST_SCAN) conecta no PRIMEIRO no encontrado, mesmo distante.
    // Varre todos os canais e escolhe o no com melhor sinal.
    WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
    WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
#endif
    WiFi.begin(ssid.c_str(), pass.c_str());
}

void start_wifi_scan() {
#if !BREWW_HAS_LVGL
    if (wifi_portal_active()) { WiFi.scanNetworks(true); return; } // nao derruba a rede do portal
#endif
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);
    // Scan asincrono (true)
    WiFi.scanNetworks(true);
}

int check_wifi_scan() {
    int16_t n = WiFi.scanComplete();
    if(n == -2) {
        // -2 significa que não tem scan rodando
        return 0;
    }
    return n; // Pode ser -1 (processando) ou n > 0 (pronto)
}

String get_scanned_ssid(int i) {
    return WiFi.SSID(i);
}



#if !BREWW_LITE
// ---- Pagina local /update: Basic Auth com usuario "breww" e o codigo de confirmacao ----
static bool uploadAuthorized = false;

static bool local_update_authorized() {
    String code;
    if (!cloud_pair_code(code)) return false; // sem chave ainda: fechado
    return server.authenticate("breww", code.c_str());
}

static void request_local_auth() {
    server.requestAuthentication(BASIC_AUTH, "Breww", "Use o usuario breww e o codigo de confirmacao da tela do aparelho");
}
#endif

void handleRoot() {
#if !BREWW_HAS_LVGL
    if (wifi_portal_active()) { wifi_portal_handle_root(); return; } // configuracao do Wi-Fi
#endif
    String html = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\"><title>Breww Controller</title>";
    html += F("<style>body{background-color:#0a0a0a;color:#ffffff;font-family:'Segoe UI',Tahoma,Geneva,Verdana,sans-serif;text-align:center;padding:10vh 20px;margin:0;}");
    html += F("h1{color:#f59e0b;font-size:2.5rem;margin-bottom:10px;}");
    html += F("p{color:#cccccc;font-size:1.1rem;}");
    html += F(".card{background-color:#111111;border:1px solid #222222;border-radius:16px;padding:40px 20px;max-width:500px;margin:0 auto;box-shadow:0 10px 30px rgba(0,0,0,0.5);}");
    html += F(".code{background:#000;padding:10px;border-radius:8px;font-family:monospace;color:#34d399;margin:20px 0;}");
    html += F("</style></head>");
    html += F("<body><div class=\"card\">");
#if !BREWW_LITE // logo de 8,7 KB: no ESP8266 nao cabe na RAM
    html += F("<img src='data:image/png;base64,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' style='max-width:200px; margin-bottom: 20px;'>");
#endif
    html += F("<p>Seu sistema de controle de fermentação está <b>ONLINE</b>.</p>");
    html += F("<hr style=\"border-color:#222; margin:30px 0;\">");
    html += F("<p style=\"font-size:0.9rem;color:#888;\">Para enviar dados do seu iSpindel, configure-o com as seguintes rotas:</p>");
    html += F("<div class=\"code\">Server Address: breww.local<br>Server URI: /ispindel</div>");
    html += F("</div></body></html>");
    server.send(200, "text/html", html);
}

void handleNotFound() {
#if !BREWW_HAS_LVGL
    if (wifi_portal_active()) { wifi_portal_redirect(); return; } // portal cativo: tudo leva a pagina
#endif
    server.send(404, "text/plain", "404: Rota nao encontrada no Breww Controller.");
}

void handleIspindel() {
    if (server.hasArg("plain") == false) { 
        server.send(400, "text/plain", "Body not received"); 
        return; 
    }
    String jsonStr = server.arg("plain");
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, jsonStr);
    
    if (error) { 
        server.send(400, "text/plain", "Invalid JSON"); 
        return; 
    }
    
    float g = doc.containsKey("gravity") ? (float)doc["gravity"] : ispindelGravity;
    float t = doc.containsKey("temperature") ? (float)doc["temperature"] : ispindelTemp;
    float b = doc.containsKey("battery") ? (float)doc["battery"] : ispindelBattery;
    
    handle_ispindel_data(g, t, b);
    
    server.send(200, "text/plain", "OK");
}

void init_network() {
    Serial.println(F("Inicializando Módulo de Rede..."));
    
    // Inicia o driver Wi-Fi para evitar crash no server.begin()
    WiFi.mode(WIFI_STA);
    // MDNS.begin("breww"); // Removido para nao conflitar com ArduinoOTA (igual ao codigo antigo!)
    
    // Fuso horario do Brasil (UTC-3). O SNTP so liga com o Wi-Fi conectado (ntp_loop).
    setenv("TZ", "<-03>3", 1);
    tzset();
    
    // TESTE WIZARD: Apagar credenciais antigas para forcar o Wizard
    
    // Tenta carregar as credenciais da memoria
     
    if(load_wifi_credentials()) {
        attempt_wifi_connection(savedSSID, savedPASS);
    }
    mqttClient.setBufferSize(2048); // telemetria com o perfil (ate 10 etapas) e setProfile
    // Conexao "meio morta" (sinal ruim): detecta em ~10-20 s em vez de ~30-45 s
    mqttClient.setKeepAlive(10);
    mqttClient.setSocketTimeout(5);
    cloud_tls_setup(espClientTls, MQTT_TLS_PORT); // valida o certificado e o nome breww.live
#if !defined(ESP8266)
    espClientTls.setHandshakeTimeout(10);      // segundos; nao travar o loop se a 8883 nao responder
#endif
    mqttClient.setServer(MQTT_SERVER, MQTT_TLS_PORT);
    mqttClient.setCallback(mqttCallback);
    server.on("/", HTTP_GET, handleRoot);
    
#if !BREWW_LITE
    // Pagina local de atualizacao: usuario "breww" + codigo de confirmacao da tela (antes era aberta
    // para qualquer um na mesma rede gravar um firmware). O OTA pela nuvem segue o caminho normal.
    server.on("/update", HTTP_GET, []() {
        if (!local_update_authorized()) { request_local_auth(); return; }
        server.sendHeader("Connection", "close");
        server.send(200, "text/html", "<form method='POST' action='/update' enctype='multipart/form-data'><input type='file' name='update'><input type='submit' value='Update'></form>");
    });
    server.on("/update", HTTP_POST, []() {
        if (!uploadAuthorized) { request_local_auth(); return; }
        server.sendHeader("Connection", "close");
        server.send(200, "text/plain", (Update.hasError()) ? "FAIL" : "OK");
        ESP.restart();
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) uploadAuthorized = local_update_authorized();
        if (!uploadAuthorized) return; // sem senha: ignora o arquivo inteiro
        if (upload.status == UPLOAD_FILE_START) { 
            Serial.printf_P(PSTR("Update: %s\n"), upload.filename.c_str());
            isUpdatingOTA = true;
            override_relays(false, false);
            ui_show_ota_screen();
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { 
                Update.printError(Serial); 
            }
        } else if (upload.status == UPLOAD_FILE_WRITE) { 
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
                Update.printError(Serial);
            }
            plat_wdt_feed();
            if (Update.size() > 0) {
                ui_update_ota_progress((Update.progress() * 100) / Update.size());
            }
        } else if (upload.status == UPLOAD_FILE_END) { 
            if (Update.end(true)) {
                Serial.printf_P(PSTR("Update Success: %u\nRebooting...\n"), upload.totalSize);
                ui_show_ota_success(true);
            } else {
                Update.printError(Serial);
                ui_show_ota_success(false);
            }
            isUpdatingOTA = false;
        }
    });
#endif

    server.onNotFound(handleNotFound);
    server.on("/ispindel", HTTP_POST, handleIspindel);
    server.begin();



    Serial.println(F("Servidor Web (iSpindel) iniciado na porta 80"));
}

bool ota_started = false;

// ---------------------------------------------------------------------------
// Diagnostico remoto (fila pequena: eventos gerados sem conexao sao enviados depois)
// ---------------------------------------------------------------------------
#if BREWW_LITE
#define DIAG_QUEUE_LEN 3 // RAM curta
#else
#define DIAG_QUEUE_LEN 8
#endif
static char diagQueue[DIAG_QUEUE_LEN][240];
static int diagHead = 0, diagCount = 0;

void diag_report(const char* json) {
    int idx = (diagHead + diagCount) % DIAG_QUEUE_LEN;
    if (diagCount == DIAG_QUEUE_LEN) { diagHead = (diagHead + 1) % DIAG_QUEUE_LEN; diagCount--; } // descarta o mais antigo
    strncpy(diagQueue[idx], json, sizeof(diagQueue[idx]) - 1);
    diagQueue[idx][sizeof(diagQueue[idx]) - 1] = '\0';
    diagCount++;
}

static void diag_flush() {
    if (!diagCount || !mqttClient.connected()) return;
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String topic = "brewbrother/" + mac + "/diag";
    for (int sent = 0; sent < 2 && diagCount; sent++) { // no maximo 2 por volta do loop
        if (!mqttClient.publish(topic.c_str(), diagQueue[diagHead], false)) break;
        diagHead = (diagHead + 1) % DIAG_QUEUE_LEN;
        diagCount--;
    }
}

// ---------------------------------------------------------------------------
// Roaming em rede mesh: o ESP32 nao troca de no sozinho. Se o sinal ficar fraco por
// 1 minuto, procura o mesmo nome de rede em todos os canais e, se houver um no pelo
// menos 8 dB melhor, reconecta nele. No maximo uma tentativa a cada 10 minutos.
// ---------------------------------------------------------------------------
static const int ROAM_WEAK_RSSI = -72;
static const unsigned long ROAM_WEAK_MS = 60000;
static const unsigned long ROAM_COOLDOWN_MS = 10UL * 60UL * 1000UL;
static const int ROAM_MIN_GAIN_DB = 8;

static void wifi_roaming_check() {
#if defined(ESP8266)
    return; // Lite: sem troca de no (a busca por canal/SSID do ESP32 nao existe aqui)
#else
    static unsigned long weakSince = 0, lastRoam = 0, lastSample = 0;
    static bool scanning = false;
    if (isUpdatingOTA || WiFi.status() != WL_CONNECTED || savedSSID.length() == 0) { weakSince = 0; return; }

    if (scanning) {
        int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;
        scanning = false;
        if (n <= 0) { WiFi.scanDelete(); return; }
        int cur = WiFi.RSSI();
        String curBssid = WiFi.BSSIDstr();
        int best = -1;
        for (int i = 0; i < n; i++) {
            if (WiFi.SSID(i) != savedSSID || WiFi.BSSIDstr(i) == curBssid) continue;
            if (best < 0 || WiFi.RSSI(i) > WiFi.RSSI(best)) best = i;
        }
        if (best >= 0 && WiFi.RSSI(best) >= cur + ROAM_MIN_GAIN_DB) {
            int toRssi = WiFi.RSSI(best);
            int channel = WiFi.channel(best);
            uint8_t bssid[6];
            memcpy(bssid, WiFi.BSSID(best), 6);
            String toBssid = WiFi.BSSIDstr(best);
            Serial.printf_P(PSTR("[WiFi] Trocando de no da mesh: %s (%d dBm) -> %s (%d dBm, canal %d)\n"),
                          curBssid.c_str(), cur, toBssid.c_str(), toRssi, channel);
            char ev[200];
            snprintf_P(ev, sizeof(ev), PSTR("{\"type\":\"wifi_roam\",\"from_rssi\":%d,\"to_rssi\":%d,\"from\":\"%s\",\"to\":\"%s\"}"),
                     cur, toRssi, curBssid.c_str(), toBssid.c_str());
            diag_report(ev);
            WiFi.scanDelete();
            lastRoam = millis();
            weakSince = 0;
            WiFi.begin(savedSSID.c_str(), savedPASS.c_str(), channel, bssid);
            extern unsigned long roamPinnedAt;
            roamPinnedAt = millis(); // o vigia volta para a rede normal se este no nao conectar
            return;
        }
        Serial.printf_P(PSTR("[WiFi] Sinal fraco (%d dBm), mas nenhum no da mesh e melhor.\n"), cur);
        WiFi.scanDelete();
        return;
    }

    if (millis() - lastSample < 5000) return;
    lastSample = millis();
    if (WiFi.RSSI() > ROAM_WEAK_RSSI) { weakSince = 0; return; }
    if (!weakSince) weakSince = millis();
    if (millis() - weakSince < ROAM_WEAK_MS || (lastRoam && millis() - lastRoam < ROAM_COOLDOWN_MS)) return;
    Serial.printf_P(PSTR("[WiFi] Sinal fraco ha 1 min (%d dBm). Procurando no melhor da mesh...\n"), WiFi.RSSI());
    lastRoam = millis(); // conta como tentativa (respeita o intervalo mesmo se nao trocar)
    weakSince = 0;
    // Busca assincrona so pela nossa rede (todos os canais, 120 ms por canal)
    if (WiFi.scanNetworks(true, false, false, 120, 0, savedSSID.c_str()) == WIFI_SCAN_RUNNING) scanning = true;
#endif
}

// ---------------------------------------------------------------------------
// Relogio (NTP). Travamento real (core dump da 2.9.7, placa com tela): o SNTP do lwIP resolvia
// "pool.ntp.org" por DNS e o dns_enqueue criava um socket UDP sem o lock da pilha TCP/IP
// (assert "Required to lock TCPIP core functionality" em udp_new -> abort). Agora o nome e
// resolvido aqui, no loop, com WiFi.hostByName (seguro), e o SNTP recebe o IP: com IP o lwIP
// nao passa pelo dns_enqueue. Liga so com o Wi-Fi conectado e renova o IP a cada 6 h.
// ---------------------------------------------------------------------------
static void ntp_loop() {
    static unsigned long lastConfig = 0;
    static bool configured = false;
    // O lwIP guarda o PONTEIRO do nome do servidor: buffers fixos, alternados a cada troca (o que o
    // SNTP em uso le nunca e reescrito)
    static char ipBuf[2][16] = {"", ""};
    static uint8_t cur = 0;
    if (WiFi.status() != WL_CONNECTED) return;
    const bool due = !configured || millis() - lastConfig > 6UL * 3600UL * 1000UL ||
                     (time(nullptr) < 1704067200 && millis() - lastConfig > 60000UL); // sem hora: tenta outro IP a cada 1 min
    if (!due) return;
    lastConfig = millis();
    static const char* const SERVERS[] = {"pool.ntp.org", "time.google.com", "a.st1.ntp.br"};
    static uint8_t next = 0;
    IPAddress ip;
    String chosen;
    for (uint8_t k = 0; k < 3 && !chosen.length(); k++) {
        const char* name = SERVERS[(next + k) % 3];
        if (WiFi.hostByName(name, ip) == 1 && ip != IPAddress((uint32_t)0)) chosen = ip.toString();
    }
    next = (next + 1) % 3;
    if (!chosen.length()) { Serial.println(F("[NTP] DNS sem resposta; tento de novo em 1 min")); return; }
    if (configured && chosen == ipBuf[cur] && time(nullptr) >= 1704067200) return; // mesmo servidor e hora certa
    const uint8_t nb = configured ? (uint8_t)(cur ^ 1) : cur;
    strlcpy(ipBuf[nb], chosen.c_str(), sizeof(ipBuf[nb]));
    configTime(-3 * 3600, 0, ipBuf[nb]); // IP, nao nome (sem DNS dentro do lwIP)
    cur = nb;
    configured = true;
    Serial.printf_P(PSTR("[NTP] Servidor de hora %s\n"), ipBuf[cur]);
}

// ---------------------------------------------------------------------------
// Vigia do Wi-Fi. A troca de no da mesh conecta FIXADA no BSSID escolhido; se esse no falhasse, a
// placa ficava tentando so ele para sempre (DevKit com sinal fraco: ligada, piscando, offline).
//  - troca de no sem conectar em 20 s -> volta para a rede normal (qualquer no);
//  - Wi-Fi salvo e 30 s desconectada -> reconecta do zero, a cada 30 s.
// Nao age enquanto o instalador/portal conecta uma rede nova (wifi_setup).
// ---------------------------------------------------------------------------
unsigned long roamPinnedAt = 0;
static void wifi_watchdog() {
    static unsigned long downSince = 0, lastKick = 0;
    if (WiFi.status() == WL_CONNECTED) { downSince = 0; roamPinnedAt = 0; return; }
    if (isUpdatingOTA || savedSSID.length() == 0 || wifi_setup_connecting()) { downSince = 0; return; }
    const unsigned long now = millis();
    if (!downSince) downSince = now;
    const bool roamFailed = roamPinnedAt && now - roamPinnedAt > 20000;
    const bool longDown = now - downSince > 30000 && (!lastKick || now - lastKick > 30000);
    if (!roamFailed && !longDown) return;
    Serial.printf_P(PSTR("[WiFi] %s: reconectando em \"%s\" (qualquer no)\n"),
                    roamFailed ? "Troca de no nao conectou" : "Sem Wi-Fi ha 30 s", savedSSID.c_str());
    char ev[120];
    snprintf_P(ev, sizeof(ev), PSTR("{\"type\":\"wifi_kick\",\"roam\":%d,\"down\":%lu}"), roamFailed ? 1 : 0, (now - downSince) / 1000);
    diag_report(ev);
    roamPinnedAt = 0;
    lastKick = now;
    WiFi.disconnect();
    WiFi.begin(savedSSID.c_str(), savedPASS.c_str()); // sem canal/BSSID: o driver escolhe o no
}

// Perfil do ultimo handle_network (ms), lido pelo detector de travamento do loop
unsigned long prof_ota = 0, prof_cloud = 0, prof_reconnect = 0, prof_mqttloop = 0, prof_telemetry = 0, prof_web = 0;

void handle_network() {
    prof_ota = prof_cloud = prof_reconnect = prof_mqttloop = prof_telemetry = prof_web = 0;
    wifi_watchdog();
    if (WiFi.status() != WL_CONNECTED) {
        ota_started = false;
        return; 
    }
    
    // Atualizacao pela IDE na rede local: senha = codigo de confirmacao do aparelho (o mesmo da
    // pagina /update). Sem chave ainda (primeiros segundos com Wi-Fi), espera; nunca senha fixa.
#if !BREWW_LITE
    String otaCode;
    if (!ota_started && cloud_pair_code(otaCode)) {
        ArduinoOTA.setHostname("breww");
        ArduinoOTA.setPassword(otaCode.c_str());
        ArduinoOTA.onStart([]() {
            String type;
            if (ArduinoOTA.getCommand() == U_FLASH) {
                type = "sketch";
            } else {
                type = "filesystem";
            }
            Serial.println("Start updating " + type);
            isUpdatingOTA = true; 
            override_relays(false, false);
            ui_show_ota_screen();
        });
        ArduinoOTA.onEnd([]() {
            Serial.println(F("\nEnd OTA"));
            ui_show_ota_success(true);
            isUpdatingOTA = false;
        });
        ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
            plat_wdt_feed();
            int p = (progress / (total / 100));
            Serial.printf_P(PSTR("Progress: %u%%\r"), p);
            ui_update_ota_progress(p);
        });
        ArduinoOTA.onError([](ota_error_t error) {
            Serial.printf_P(PSTR("Error[%u]: "), error);
            if (error == OTA_AUTH_ERROR) Serial.println(F("Auth Failed"));
            else if (error == OTA_BEGIN_ERROR) Serial.println(F("Begin Failed"));
            else if (error == OTA_CONNECT_ERROR) Serial.println(F("Connect Failed"));
            else if (error == OTA_RECEIVE_ERROR) Serial.println(F("Receive Failed"));
            else if (error == OTA_END_ERROR) Serial.println(F("End Failed"));
            ui_show_ota_success(false);
            isUpdatingOTA = false;
        });
        ArduinoOTA.begin();
        ota_started = true;
        Serial.println(F("ArduinoOTA Pronto e Escutando!"));
    }
#endif
    
    ntp_loop();
    wifi_roaming_check();
    unsigned long tp = millis();
#if !BREWW_LITE
    if (ota_started) ArduinoOTA.handle();
#endif
    prof_ota = millis() - tp; tp = millis();
    cloud_pairing_loop();
    prof_cloud = millis() - tp;

    static unsigned long mqttDownSince = 0;
    static int mqttAttempts = 0;
    static bool bootReported = false;
    if (!mqttClient.connected()) {
        long now = millis();
        if (!mqttDownSince) mqttDownSince = millis();
        if (now - lastMqttReconnectAttempt > 5000) {
            lastMqttReconnectAttempt = now;
            tp = millis();
            Serial.println(F("[MQTT] Conexao perdida, reconectando..."));
            mqttAttempts++;
            int stBefore = mqttClient.state();
            if (reconnectMqtt()) {
                lastMqttReconnectAttempt = 0;
                char ev[200];
                if (!bootReported) {
                    bootReported = true;
                    snprintf_P(ev, sizeof(ev), PSTR("{\"type\":\"boot\",\"reason\":\"%s\",\"fw\":\"%s\",\"ms\":%lu,\"heap\":%u,\"otaTest\":%d,\"otaRolledBack\":%d}"),
                             plat_reset_reason(), FW_VERSION, millis(),
                             (unsigned)plat_free_heap(),
                             ota_guard_pending() ? 1 : 0, ota_guard_rolled_back_once() ? 1 : 0);
                } else {
                    snprintf_P(ev, sizeof(ev), PSTR("{\"type\":\"mqtt_reconnect\",\"ms\":%lu,\"down\":%lu,\"attempts\":%d,\"state\":%d,\"rssi\":%d}"),
                             millis() - tp, millis() - mqttDownSince, mqttAttempts, stBefore, (int)WiFi.RSSI());
                }
                diag_report(ev);
                mqttDownSince = 0;
                mqttAttempts = 0;
            }
            prof_reconnect = millis() - tp;
        }
    } else {
        tp = millis();
        mqttClient.loop();
        prof_mqttloop = millis() - tp;
        diag_flush();
        
        
        // Event-based telemetry: envia apenas quando algo realmente muda, ou a cada 30s (Heartbeat)
        static float lastReportedTemp1 = -999;
        static float lastReportedTemp2 = -999;
        static bool lastReportedHeater = false;
        static bool lastReportedCooler = false;
        
        bool needsReport = false;
        // Só permite enviar por evento se já passou pelo menos 2 segundos desde o último envio
        if (millis() - lastTelemetry > 2000) {
            if (abs(currentTemp - lastReportedTemp1) >= 0.1) needsReport = true;
            if (abs(currentTemp2 - lastReportedTemp2) >= 0.1) needsReport = true;
            if (isHeaterOn != lastReportedHeater) needsReport = true;
            if (isCoolerOn != lastReportedCooler) needsReport = true;
        }
        
        if (needsReport || millis() - lastTelemetry > 3000) {
            lastReportedTemp1 = currentTemp;
            lastReportedTemp2 = currentTemp2;
            lastReportedHeater = isHeaterOn;
            lastReportedCooler = isCoolerOn;
            lastTelemetry = millis();
            tp = millis();
            mqtt_publish_telemetry();
            prof_telemetry = millis() - tp;
        }
    }
    
    tp = millis();
    server.handleClient();
    prof_web = millis() - tp;
}

// Hardware detectado (Fase 2, item 9) para a tela "Hardware" do app, ex.: ["oled","ds18b20:2","dht22"]
static void hardware_modules_json(char* out, size_t n) {
    size_t len = 0;
    auto add = [&](const char* m) {
        if (len + strlen(m) + 4 >= n) return;
        len += snprintf_P(out + len, n - len, PSTR("%s\"%s\""), len > 1 ? "," : "", m);
    };
    len = snprintf_P(out, n, PSTR("["));
#if BREWW_HAS_LVGL
    add("tela35");
#else
    if (small_display_kind()) add(small_display_kind());
#endif
    char ds[16];
    snprintf_P(ds, sizeof(ds), PSTR("ds18b20:%d"), ds18b20_count());
    add(ds);
    if (i2c_humidity_sensor()) add(i2c_humidity_sensor());
    else if (!isnan(currentHumidity)) add("dht22");
    snprintf_P(out + len, n - len, PSTR("]"));
}

void mqtt_publish_device_action(const char* json) {
    if (!mqttClient.connected()) return;
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mqttClient.publish(("brewbrother/" + mac + "/device_action").c_str(), json);
}

void mqtt_publish_telemetry() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    String topic = "brewbrother/" + mac + "/data";
    
    // Umidade invalida (NAN) vira null: "%.1f" de NAN geraria "nan", que eh JSON invalido
    char humStr[16];
    if (isnan(currentHumidity)) strcpy(humStr, "null");
    else snprintf_P(humStr, sizeof(humStr), PSTR("%.1f"), currentHumidity);

    // Quando chegou a ultima leitura do iSpindel (epoch, s). Os valores is_sg/is_bat ficam guardados
    // e sao repetidos em todo pacote; com is_epc o app/site sabem se a leitura e recente.
    // 0 = desconhecido (nunca recebeu, ou recebeu antes do relogio sincronizar e ainda nao da
    // para calcular).
    unsigned long ispEpoch = 0;
    time_t nowEpoch = time(NULL);
    if (lastIspindelEpoch > 1600000000UL) {
        ispEpoch = lastIspindelEpoch;
    } else if (lastIspindelUpdate > 0 && nowEpoch > 1600000000L) {
        ispEpoch = (unsigned long)nowEpoch - (millis() - lastIspindelUpdate) / 1000UL;
    }

    // Perfil de fermentacao (so com perfil ativo, para o padrao de fabrica nunca sobrescrever
    // o lote no app/site): etapas {n,t,d(horas)}, etapa atual, horas na etapa e pausa.
    static char profile[900]; // estatico: fora da pilha (ESP8266 tem 4 KB)
    profile[0] = '\0';
    if (isRampActive && numRampSteps > 0) {
        size_t len = snprintf_P(profile, sizeof(profile), PSTR("\"steps\":["));
        for (int i = 0; i < numRampSteps && len < sizeof(profile) - 80; i++) {
            char nameEsc[48];
            size_t k = 0;
            for (const char *c = rampSteps[i].name; *c && k < sizeof(nameEsc) - 2; c++) {
                if (*c == '"' || *c == '\\') nameEsc[k++] = '\\';
                nameEsc[k++] = *c;
            }
            nameEsc[k] = '\0';
            len += snprintf(profile + len, sizeof(profile) - len, "%s{\"n\":\"%s\",\"t\":%.1f,\"d\":%.1f}",
                            i ? "," : "", nameEsc, rampSteps[i].targetTemp, rampSteps[i].durationDays * 24.0f);
        }
        snprintf(profile + len, sizeof(profile) - len, "],\"currStep\":%d,\"stepTime\":%.2f,\"isPaused\":%s, ",
                 currentRampStep, ramp_step_elapsed_hours(), isRampPaused ? "true" : "false");
    } else {
        strcpy(profile, "\"isPaused\":false, ");
    }

    char mods[96];
    hardware_modules_json(mods, sizeof(mods));
    char pinsJson[128];
    pins_to_json(pinsJson, sizeof(pinsJson));
    char pinsErr[160] = "";
    if (const char* pe = pins_last_error()) {
        String esc = pe; esc.replace("\\", "/"); esc.replace("\"", "'");
        snprintf_P(pinsErr, sizeof(pinsErr), PSTR(", \"pinsErr\":\"%s\""), esc.c_str());
    }
    static char payload[1800]; // estatico: fora da pilha do loop
    snprintf_P(payload, sizeof(payload), PSTR("{\"ferm\":%.2f, \"amb\":%.2f, \"csp\":%.2f, \"opm\":%d, \"is_sg\":%.3f, \"is_bat\":%.2f, \"is_epc\":%lu, \"statOp\":\"%s\", \"rssi\":%ld, \"fermRun\":%d, %s\"ts\":%lu, \"c_n1\":\"%s\", \"c_n3\":\"%s\", \"c_ibu\":%d, \"c_vol\":%.1f, \"ext_s\":{\"hum\":%s}, \"macCtrl\":\"%s\", \"mac2\":\"%s\", \"fw\":\"%s\", \"board\":\"%s\", \"relInv\":%d, \"sensOk\":%d, \"mods\":%s, \"pins\":%s%s}"),
             currentTemp, currentTemp2, currentSetpoint, currentMode, ispindelGravity, ispindelBattery, ispEpoch,
             (isHeaterOn ? "Aquecendo" : (isCoolerOn ? "Resfriando" : "Desligado")),
             (long)WiFi.RSSI(), (isRampActive && !isRampPaused) ? 1 : 0, profile, (unsigned long)time(NULL),
             beerName.c_str(), beerType.c_str(), beerIBU, beerABV, humStr, sensorMacCtrl.c_str(), sensorMac2.c_str(), FW_VERSION, BREWW_BOARD_NAME,
             (relay_heater_inverted() ? 1 : 0) | (relay_cooler_inverted() ? 2 : 0), // bit0 aquecer, bit1 resfriar
             sensor_fresh(currentMode == MODE_FERM ? 0 : 1) ? 1 : 0, // 0 = sem leitura do sensor de controle (reles desligados)
             mods, pinsJson, pinsErr);

    if (!mqttClient.publish(topic.c_str(), payload, true) && mqttClient.connected()) {
        // Envio falhou com a conexao "aberta": provavelmente travada. Forca reconexao ja.
        static int publishFailures = 0;
        if (++publishFailures >= 2) {
            publishFailures = 0;
            Serial.println(F("[MQTT] Envio falhou 2x; conexao travada. Reconectando."));
            mqttClient.disconnect();
        }
    }
}
bool has_wifi_credentials() { return savedSSID.length() > 0; }
