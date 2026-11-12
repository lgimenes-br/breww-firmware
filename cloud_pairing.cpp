// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "cloud_pairing.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_mac.h>
#include <esp_random.h>
#include <PubSubClient.h>
#include "mbedtls/md.h"
#include <esp_heap_caps.h>
#include "mbedtls/platform.h"
#include <time.h>
#include <atomic>
#include "ui.h"

// ---------------------------------------------------------------------------
// Pareamento seguro com o servidor
//  - A placa gera uma chave secreta aleatoria (256 bits) e guarda na NVS, num namespace
//    proprio ("breww_id") que o reset de configuracao nao apaga.
//  - Toda chamada vai por HTTPS com certificado VALIDADO (raizes ISRG/Let's Encrypt),
//    para a chave nao vazar. O servidor registra a chave no primeiro contato.
//  - O MQTT so "acorda" a placa (pair_request); os dados do pedido e a confirmacao
//    trafegam por HTTPS autenticado com a chave.
// ---------------------------------------------------------------------------

#define CLOUD_API_BASE "https://breww.live/api"
static const unsigned long ANNOUNCE_INTERVAL_MS = 5UL * 60UL * 1000UL;
static const unsigned long ANNOUNCE_RETRY_MS = 30UL * 1000UL;
// Sem vinculo ainda nao ha MQTT para "acordar" o aparelho: anuncia mais vezes para o pedido
// de vinculo aparecer na tela em segundos
static const unsigned long ANNOUNCE_UNBOUND_MS = 10UL * 1000UL;
// Chave recusada (aparelho resetado): continua tentando, para o dono poder trocar a chave
static const unsigned long ANNOUNCE_REJECTED_MS = 60UL * 1000UL;

// ISRG Root X1 + ISRG Root X2 (Let's Encrypt). Validos ate 2035/2040.
static const char ISRG_ROOT_CAS[] = R"PEM(
-----BEGIN CERTIFICATE-----
MIIFazCCA1OgAwIBAgIRAIIQz7DSQONZRGPgu2OCiwAwDQYJKoZIhvcNAQELBQAw
TzELMAkGA1UEBhMCVVMxKTAnBgNVBAoTIEludGVybmV0IFNlY3VyaXR5IFJlc2Vh
cmNoIEdyb3VwMRUwEwYDVQQDEwxJU1JHIFJvb3QgWDEwHhcNMTUwNjA0MTEwNDM4
WhcNMzUwNjA0MTEwNDM4WjBPMQswCQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJu
ZXQgU2VjdXJpdHkgUmVzZWFyY2ggR3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBY
MTCCAiIwDQYJKoZIhvcNAQEBBQADggIPADCCAgoCggIBAK3oJHP0FDfzm54rVygc
h77ct984kIxuPOZXoHj3dcKi/vVqbvYATyjb3miGbESTtrFj/RQSa78f0uoxmyF+
0TM8ukj13Xnfs7j/EvEhmkvBioZxaUpmZmyPfjxwv60pIgbz5MDmgK7iS4+3mX6U
A5/TR5d8mUgjU+g4rk8Kb4Mu0UlXjIB0ttov0DiNewNwIRt18jA8+o+u3dpjq+sW
T8KOEUt+zwvo/7V3LvSye0rgTBIlDHCNAymg4VMk7BPZ7hm/ELNKjD+Jo2FR3qyH
B5T0Y3HsLuJvW5iB4YlcNHlsdu87kGJ55tukmi8mxdAQ4Q7e2RCOFvu396j3x+UC
B5iPNgiV5+I3lg02dZ77DnKxHZu8A/lJBdiB3QW0KtZB6awBdpUKD9jf1b0SHzUv
KBds0pjBqAlkd25HN7rOrFleaJ1/ctaJxQZBKT5ZPt0m9STJEadao0xAH0ahmbWn
OlFuhjuefXKnEgV4We0+UXgVCwOPjdAvBbI+e0ocS3MFEvzG6uBQE3xDk3SzynTn
jh8BCNAw1FtxNrQHusEwMFxIt4I7mKZ9YIqioymCzLq9gwQbooMDQaHWBfEbwrbw
qHyGO0aoSCqI3Haadr8faqU9GY/rOPNk3sgrDQoo//fb4hVC1CLQJ13hef4Y53CI
rU7m2Ys6xt0nUW7/vGT1M0NPAgMBAAGjQjBAMA4GA1UdDwEB/wQEAwIBBjAPBgNV
HRMBAf8EBTADAQH/MB0GA1UdDgQWBBR5tFnme7bl5AFzgAiIyBpY9umbbjANBgkq
hkiG9w0BAQsFAAOCAgEAVR9YqbyyqFDQDLHYGmkgJykIrGF1XIpu+ILlaS/V9lZL
ubhzEFnTIZd+50xx+7LSYK05qAvqFyFWhfFQDlnrzuBZ6brJFe+GnY+EgPbk6ZGQ
3BebYhtF8GaV0nxvwuo77x/Py9auJ/GpsMiu/X1+mvoiBOv/2X/qkSsisRcOj/KK
NFtY2PwByVS5uCbMiogziUwthDyC3+6WVwW6LLv3xLfHTjuCvjHIInNzktHCgKQ5
ORAzI4JMPJ+GslWYHb4phowim57iaztXOoJwTdwJx4nLCgdNbOhdjsnvzqvHu7Ur
TkXWStAmzOVyyghqpZXjFaH3pO3JLF+l+/+sKAIuvtd7u+Nxe5AW0wdeRlN8NwdC
jNPElpzVmbUq4JUagEiuTDkHzsxHpFKVK7q4+63SM1N95R1NbdWhscdCb+ZAJzVc
oyi3B43njTOQ5yOf+1CceWxG1bQVs5ZufpsMljq4Ui0/1lvh+wjChP4kqKOJ2qxq
4RgqsahDYVvTH9w7jXbyLeiNdd8XM2w9U/t7y0Ff/9yi0GE44Za4rF2LN9d11TPA
mRGunUHBcnWEvgJBQl9nJEiU0Zsnvgc/ubhPgXRR4Xq37Z0j4r7g1SgEEzwxA57d
emyPxgcYxn/eR44/KJ4EBs+lVDR3veyJm+kXQ99b21/+jh5Xos1AnX5iItreGCc=
-----END CERTIFICATE-----
-----BEGIN CERTIFICATE-----
MIICGzCCAaGgAwIBAgIQQdKd0XLq7qeAwSxs6S+HUjAKBggqhkjOPQQDAzBPMQsw
CQYDVQQGEwJVUzEpMCcGA1UEChMgSW50ZXJuZXQgU2VjdXJpdHkgUmVzZWFyY2gg
R3JvdXAxFTATBgNVBAMTDElTUkcgUm9vdCBYMjAeFw0yMDA5MDQwMDAwMDBaFw00
MDA5MTcxNjAwMDBaME8xCzAJBgNVBAYTAlVTMSkwJwYDVQQKEyBJbnRlcm5ldCBT
ZWN1cml0eSBSZXNlYXJjaCBHcm91cDEVMBMGA1UEAxMMSVNSRyBSb290IFgyMHYw
EAYHKoZIzj0CAQYFK4EEACIDYgAEzZvVn4CDCuwJSvMWSj5cz3es3mcFDR0HttwW
+1qLFNvicWDEukWVEYmO6gbf9yoWHKS5xcUy4APgHoIYOIvXRdgKam7mAHf7AlF9
ItgKbppbd9/w+kHsOdx1ymgHDB/qo0IwQDAOBgNVHQ8BAf8EBAMCAQYwDwYDVR0T
AQH/BAUwAwEB/zAdBgNVHQ4EFgQUfEKWrt5LSDv6kviejM9ti6lyN5UwCgYIKoZI
zj0EAwMDaAAwZQIwe3lORlCEwkSHRhtFcP9Ymd70/aTSVaYgLXTWNLxBo1BfASdW
tL4ndQavEi51mI38AjEAi/V3bNTIZargCyzuFJ0nN6T5U6VR5CmD1/iQMVtCnwr1
/q4AaOeMSQ+2b1tbFfLn
-----END CERTIFICATE-----
)PEM";

// ---------------------------------------------------------------------------
// Memoria do TLS: a RAM interna fica quase toda com a tela (LVGL) e nao sobra bloco
// contiguo para o handshake. Durante NOSSAS chamadas HTTPS o mbedTLS aloca na PSRAM;
// fora delas volta ao alocador padrao (Wi-Fi e outros usuarios do mbedTLS precisam
// de RAM interna).
// ---------------------------------------------------------------------------

static void* tls_calloc(size_t n, size_t size) {
    void* p = heap_caps_calloc(n, size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    return p;
}
static void tls_free(void* p) { heap_caps_free(p); }

// Equivalente ao padrao deste core (CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC): RAM interna
static void* internal_calloc(size_t n, size_t size) {
    return heap_caps_calloc(n, size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
}

// Contagem de referencias: com duas conexoes TLS ao mesmo tempo (ex: anuncio na tarefa
// de fundo + download da OTA), o alocador padrao so volta quando a ULTIMA terminar.
// Antes, a primeira a terminar restaurava a RAM interna no meio da outra.
static std::atomic<int> tlsScopeDepth{0};

static volatile bool cloudPaused = false; // OTA em andamento
void cloud_pause_background(bool paused) { cloudPaused = paused; }
struct TlsPsramScope { // (campo "active" publico: usado no diagnostico da OTA)
    bool active;
    // Preferimos RAM interna: com o TLS na PSRAM, o acelerador AES precisa de um buffer
    // DMA interno temporario a cada bloco ("esp-aes: Failed to allocate memory" quando falta).
    // So vai para a PSRAM se a RAM interna estiver sem bloco contiguo para o handshake.
    TlsPsramScope() : active(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) > 0 &&
                             heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) < 45000) {
        if (active && tlsScopeDepth.fetch_add(1) == 0) mbedtls_platform_set_calloc_free(tls_calloc, tls_free);
    }
    ~TlsPsramScope() {
        if (active && tlsScopeDepth.fetch_sub(1) == 1) mbedtls_platform_set_calloc_free(internal_calloc, tls_free);
    }
};

const char* cloud_root_cas() { return ISRG_ROOT_CAS; }

String get_device_id() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char buf[13];
    snprintf(buf, sizeof(buf), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    return String(buf);
}

static String deviceKey;

static const String& get_device_key() {
    if (deviceKey.length() == 64) return deviceKey;
    Preferences p;
    p.begin("breww_id", false);
    String k = p.getString("dev_key", "");
    if (k.length() != 64) {
        // Gerada so com Wi-Fi ligado (o RNG de hardware tem entropia real nesse momento)
        uint8_t raw[32];
        esp_fill_random(raw, sizeof(raw));
        char hex[65];
        for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", raw[i]);
        hex[64] = '\0';
        k = String(hex);
        p.putString("dev_key", k);
        Serial.println("[Cloud] Nova chave do aparelho gerada.");
    }
    p.end();
    deviceKey = k;
    return deviceKey;
}

// ---------------------------------------------------------------------------
// Credencial MQTT propria: usuario = ID do aparelho, senha = HMAC-SHA256(chave, "breww-mqtt-v1")
// (mesmo calculo de backend/services/mqttCredentials.js). A chave nunca vai pelo MQTT.
// ---------------------------------------------------------------------------
static int mqttCredsState = -1; // -1 nao carregado, 0 nao, 1 sim

static bool load_mqtt_flag() {
    if (mqttCredsState == -1) {
        Preferences p;
        p.begin("breww_id", true);
        mqttCredsState = p.getBool("mqtt_ok", false) ? 1 : 0;
        p.end();
    }
    return mqttCredsState == 1;
}

static void save_mqtt_flag(bool ok) {
    mqttCredsState = ok ? 1 : 0;
    Preferences p;
    p.begin("breww_id", false);
    p.putBool("mqtt_ok", ok);
    p.end();
}

static String hmac_hex(const String& key, const char* ctx);

static String derive_mqtt_password() {
    return hmac_hex(get_device_key(), "breww-mqtt-v1");
}

static String hmac_hex(const String& key, const char* ctx) {
    uint8_t out[32];
    mbedtls_md_context_t md;
    mbedtls_md_init(&md);
    mbedtls_md_setup(&md, mbedtls_md_info_from_type(MBEDTLS_MD_SHA256), 1);
    mbedtls_md_hmac_starts(&md, (const unsigned char*)key.c_str(), key.length());
    mbedtls_md_hmac_update(&md, (const unsigned char*)ctx, strlen(ctx));
    mbedtls_md_hmac_finish(&md, out);
    mbedtls_md_free(&md);
    char hex[65];
    for (int i = 0; i < 32; i++) sprintf(hex + i * 2, "%02x", out[i]);
    hex[64] = '\0';
    return String(hex);
}

bool cloud_pair_code(String& out) {
    // Nao gera a chave aqui (so le): ela nasce no loop com o Wi-Fi ligado
    String k = deviceKey;
    if (k.length() != 64) {
        Preferences p;
        p.begin("breww_id", true);
        k = p.getString("dev_key", "");
        p.end();
    }
    if (k.length() != 64) return false;
    out = hmac_hex(k, "breww-pair-v1").substring(0, 8);
    out.toUpperCase();
    return true;
}

bool cloud_mqtt_credentials(String& user, String& pass) {
    if (!load_mqtt_flag() || get_device_key().length() != 64) return false;
    user = get_device_id();
    pass = derive_mqtt_password();
    return true;
}

static bool checkRequested = false;

void cloud_mqtt_credentials_rejected() {
    Serial.println("[Cloud] Broker recusou a credencial propria; voltando ao legado e pedindo novo registro.");
    save_mqtt_flag(false);
    checkRequested = true; // o proximo anuncio recria a credencial no servidor
}

// POST JSON autenticado. Retorna o status HTTP (ou <0 em erro de conexao/TLS).
static int api_post_impl(const char* path, JsonDocument& body, JsonDocument& out) {
    WiFiClientSecure client;
    client.setCACert(ISRG_ROOT_CAS);
    HTTPClient http;
    http.setConnectTimeout(8000);
    http.setTimeout(8000);
    if (!http.begin(client, String(CLOUD_API_BASE) + path)) return -1;
    http.addHeader("Content-Type", "application/json");

    body["serial"] = get_device_id();
    body["key"] = get_device_key();
    String payload;
    serializeJson(body, payload);

    int code = http.POST(payload);
    if (code > 0) {
        DeserializationError err = deserializeJson(out, http.getString());
        if (err) out.clear();
    }
    if (code != 200) {
        Serial.printf("[Cloud] POST %s -> %d (%s)\n", path, code, http.errorToString(code).c_str());
        if (code < 0) {
            // Diagnostico de TLS: motivo do mbedTLS, memoria interna e relogio
            char tlsErr[128] = {0};
            int tlsCode = client.lastError(tlsErr, sizeof(tlsErr));
            time_t nowTs = time(nullptr);
            Serial.printf("[Cloud] TLS: %d %s | heap interno livre %u, maior bloco %u | PSRAM livre %u | hora %ld\n",
                          tlsCode, tlsErr,
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                          (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                          (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                          (long)nowTs);
        }
    }
    http.end();
    return code;
}

// Escopo garante que o cliente TLS (e toda a memoria dele) e destruido ANTES de
// restaurar o alocador padrao
static int api_post(const char* path, JsonDocument& body, JsonDocument& out) {
    TlsPsramScope scope;
    return api_post_impl(path, body, out);
}

// ===========================================================================
// HTTPS fora do loop principal
// O handshake TLS leva 1-4 s. Se rodasse no loop, MQTT (comandos) e tela ficariam
// travados nesse tempo. A tarefa "cloud" (core 0, pilha na PSRAM) so faz rede:
//  - nada de NVS/flash nela (tarefa com pilha na PSRAM nao pode desabilitar o cache)
//  - nada de LVGL nela (nao e thread-safe)
// O loop principal envia pedidos e consome os resultados sob um mutex.
// ===========================================================================
static SemaphoreHandle_t cloudMutex = nullptr;
static TaskHandle_t cloudTask = nullptr;
static bool cloudTaskFailed = false;

// Pedidos (loop principal -> tarefa)
static bool reqAnnounce = false;
static String reqLocalIp;
static bool reqRespond = false;
static String reqPairingId;
static bool reqAccept = false;

// Resultados (tarefa -> loop principal)
static bool resAnnounceReady = false;
static int resAnnounceCode = 0;
static bool resMqttReady = false;
static bool resHasPairing = false;
static bool resBound = true;
static String resPairingId, resEmail, resName;
static int resExpiresIn = 0;
static bool resRespondReady = false;
static int resRespondCode = 0;
static String resRespondStatus;

static void cloud_task_fn(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        if (cloudPaused) continue; // OTA em andamento: nao disputar memoria/rede

        bool doRespond = false, doAnnounce = false, accept = false;
        String pairingId, localIp;
        xSemaphoreTake(cloudMutex, portMAX_DELAY);
        if (reqRespond) {
            doRespond = true; pairingId = reqPairingId; accept = reqAccept; reqRespond = false;
        } else if (reqAnnounce) {
            doAnnounce = true; localIp = reqLocalIp; reqAnnounce = false;
        }
        xSemaphoreGive(cloudMutex);

        if (doRespond) {
            StaticJsonDocument<256> body;
            StaticJsonDocument<256> out;
            body["pairingId"] = pairingId;
            body["accept"] = accept;
            int code = api_post("/device/pairing/respond", body, out);
            String status = out["status"] | "";
            xSemaphoreTake(cloudMutex, portMAX_DELAY);
            resRespondCode = code; resRespondStatus = status; resRespondReady = true;
            xSemaphoreGive(cloudMutex);
        } else if (doAnnounce) {
            StaticJsonDocument<128> body;
            StaticJsonDocument<512> out;
            body["localIp"] = localIp;
            unsigned long tA = millis();
            int code = api_post("/device/announce", body, out);
            Serial.printf("[Cloud] Anuncio HTTPS (tarefa de fundo): %d em %lu ms\n", code, millis() - tA);
            xSemaphoreTake(cloudMutex, portMAX_DELAY);
            resAnnounceCode = code;
            resMqttReady = out["mqtt"] | false;
            resBound = out["bound"] | true; // servidor antigo nao manda "bound": trata como vinculado
            JsonVariant pairing = out["pairing"];
            resHasPairing = code == 200 && !pairing.isNull();
            resPairingId = resHasPairing ? String(pairing["id"] | "") : String("");
            resEmail = resHasPairing ? String(pairing["email"] | "") : String("");
            resName = resHasPairing ? String(pairing["deviceName"] | "") : String("");
            resExpiresIn = resHasPairing ? (int)(pairing["expiresIn"] | 120) : 0;
            resAnnounceReady = true;
            xSemaphoreGive(cloudMutex);
        }
    }
}

static bool ensure_cloud_task() {
    if (cloudTask) return true;
    if (cloudTaskFailed) return false;
    cloudMutex = xSemaphoreCreateMutex();
    BaseType_t ok = xTaskCreatePinnedToCoreWithCaps(cloud_task_fn, "cloud", 12288, nullptr, 1, &cloudTask, 0,
                                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (ok != pdPASS || !cloudMutex) {
        cloudTaskFailed = true;
        Serial.println("[Cloud] ERRO: nao foi possivel criar a tarefa de rede.");
        return false;
    }
    return true;
}

// OTA propria (substitui o HTTPUpdate do core). O HTTPUpdate, ANTES de pedir o arquivo,
// le o firmware atual inteiro da flash 3-4 vezes (validacao da imagem + MD5 + SHA-256)
// so para montar cabecalhos informativos que o servidor nao usa: eram ~20 s parados.
// Aqui: GET -> confere x-MD5 -> grava em blocos -> Update.end() valida o MD5.
static uint8_t otaBuf[4096]; // estatico: fora da pilha do loop

bool cloud_ota_update(const String& url, void (*onProgress)(int current, int total), String* errorOut) {
    auto fail = [&](const String& msg, WiFiClientSecure* c) -> bool {
        char tlsErr[100] = {0};
        int tlsCode = c ? c->lastError(tlsErr, sizeof(tlsErr)) : 0;
        Serial.printf("[OTA] Falhou: %s | TLS %d %s | heap interno %u (maior bloco %u) | RSSI %d\n",
                      msg.c_str(), tlsCode, tlsErr,
                      (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                      (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                      (int)WiFi.RSSI());
        if (errorOut) *errorOut = msg + (tlsCode ? String(" (TLS: ") + tlsErr + ")" : String(""));
        return false;
    };

    if (!url.startsWith("https://breww.live/")) return fail("URL nao permitida", nullptr);

    unsigned long t0 = millis();
    TlsPsramScope scope;
    WiFiClientSecure client;
    client.setCACert(ISRG_ROOT_CAS);
    client.setHandshakeTimeout(15);
    HTTPClient http;
    http.useHTTP10(true);            // sem chunked encoding
    http.setConnectTimeout(15000);
    http.setTimeout(20000);          // tolera pausas (gravacao na flash / Wi-Fi oscilando)
    const char* headerKeys[] = {"x-MD5"};
    http.collectHeaders(headerKeys, 1);
    if (!http.begin(client, url)) return fail("URL invalida", &client);

    int code = http.GET();
    unsigned long tHeaders = millis();
    if (code != 200) {
        String msg = code > 0 ? String("HTTP ") + code : String("conexao: ") + http.errorToString(code);
        http.end();
        return fail(msg, &client);
    }
    int len = http.getSize();
    String md5 = http.header("x-MD5");
    if (len <= 0) { http.end(); return fail("tamanho desconhecido", &client); }
    if (md5.length() != 32) { http.end(); return fail("servidor nao enviou o MD5", &client); }
    Serial.printf("[OTA] Servidor respondeu em %lu ms (%d bytes, md5 %s)\n", tHeaders - t0, len, md5.c_str());

    unsigned long tBegin0 = millis();
    if (!Update.begin(len, U_FLASH)) { http.end(); return fail(String("Update.begin: ") + Update.errorString(), &client); }
    Serial.printf("[OTA] Update.begin %lu ms | TLS do download na %s\n", millis() - tBegin0, scope.active ? "PSRAM" : "RAM interna");
    Update.setMD5(md5.c_str());
    if (onProgress) onProgress(0, len);

    WiFiClient* stream = http.getStreamPtr();
    int written = 0;
    unsigned long lastData = millis();
    // Diagnostico de desempenho (em ms): onde o tempo do download esta indo
    unsigned long tRead = 0, tWrite = 0, tCallback = 0, tWait = 0, reads = 0, waits = 0;
    unsigned long tDl0 = millis();
    int nextReport = 10;
    while (written < len) {
        unsigned long a = millis();
        size_t avail = stream->available();
        if (avail) {
            int want = (int)min(avail, sizeof(otaBuf));
            if (want > len - written) want = len - written;
            int r = stream->readBytes(otaBuf, want);
            unsigned long b = millis();
            tRead += b - a;
            if (r > 0) {
                reads++;
                if ((int)Update.write(otaBuf, r) != r) {
                    Update.abort(); http.end();
                    return fail(String("gravacao: ") + Update.errorString(), &client);
                }
                unsigned long c = millis();
                tWrite += c - b;
                written += r;
                lastData = c;
                if (onProgress) onProgress(written, len);
                tCallback += millis() - c;
                int pct = (int)(((long long)written * 100) / len);
                if (pct >= nextReport) {
                    unsigned long el = millis() - tDl0;
                    Serial.printf("[OTA] %3d%% | %lu ms | %lu KB/s | leitura %lu, flash %lu, MQTT+tela %lu, espera %lu ms (%lu esperas) | %lu leituras, media %lu B | RSSI %d\n",
                                  pct, el, el ? (unsigned long)written / el : 0, tRead, tWrite, tCallback, tWait, waits,
                                  reads, reads ? (unsigned long)written / reads : 0, (int)WiFi.RSSI());
                    nextReport += 10;
                }
            }
        } else {
            if (!client.connected() && !stream->available()) break;
            if (millis() - lastData > 20000) break;
            delay(1);
            waits++;
            tWait += millis() - a;
        }
    }
    unsigned long tDone = millis();
    http.end();

    if (written != len) {
        Update.abort();
        return fail(String("download incompleto (") + written + " de " + len + " bytes)", &client);
    }
    if (!Update.end()) return fail(String("verificacao: ") + Update.errorString(), &client); // inclui MD5
    Serial.printf("[OTA] Firmware gravado e conferido (MD5). Resposta %lu ms, download %lu ms, total %lu ms\n",
                  tHeaders - t0, tDone - tHeaders, millis() - t0);
    return true;
}

static unsigned long nextAnnounceAt = 0;
static bool announceInFlight = false;
static int pendingResponse = -1;  // -1 nenhuma, 0 recusar, 1 confirmar
static String currentPairingId = "";
static unsigned long pairingExpiresAt = 0;
static bool keyRejected = false;

void cloud_pairing_request_check() { checkRequested = true; }

void cloud_pairing_respond(bool accept) {
    if (currentPairingId.length()) pendingResponse = accept ? 1 : 0;
}

void cloud_pairing_loop() {
    if (!ensure_cloud_task()) return;
    get_device_key(); // NVS so aqui no loop principal (a tarefa usa o valor em cache)
    unsigned long now = millis();

    // --- Resultados da tarefa de rede ---
    bool announceReady = false, respondReady = false, mqttReady = false, hasPairing = false, bound = true;
    int announceCode = 0, respondCode = 0, expiresIn = 0;
    String pairingId, email, name, respondStatus;
    xSemaphoreTake(cloudMutex, portMAX_DELAY);
    if (resAnnounceReady) {
        announceReady = true; resAnnounceReady = false;
        announceCode = resAnnounceCode; mqttReady = resMqttReady; hasPairing = resHasPairing;
        bound = resBound;
        pairingId = resPairingId; email = resEmail; name = resName; expiresIn = resExpiresIn;
    }
    if (resRespondReady) {
        respondReady = true; resRespondReady = false;
        respondCode = resRespondCode; respondStatus = resRespondStatus;
    }
    xSemaphoreGive(cloudMutex);

    if (respondReady) ui_pair_result(respondStatus.c_str(), respondCode > 0);

    if (announceReady) {
        announceInFlight = false;
        if (announceCode == 200) {
            keyRejected = false;
            nextAnnounceAt = now + (bound ? ANNOUNCE_INTERVAL_MS : ANNOUNCE_UNBOUND_MS);
            if (!mqttReady && load_mqtt_flag() && !bound) save_mqtt_flag(false); // chave ainda nao vinculada
            if (mqttReady && !load_mqtt_flag()) {
                save_mqtt_flag(true);
                Serial.println("[Cloud] Credencial MQTT propria pronta; reconectando ao broker com ela.");
                extern PubSubClient mqttClient;
                mqttClient.disconnect(); // o loop reconecta ja com usuario/senha do aparelho
            }
            if (!hasPairing) {
                if (currentPairingId.length()) { ui_hide_pair_confirm(); currentPairingId = ""; }
            } else if (pairingId.length() && pairingId != currentPairingId) {
                currentPairingId = pairingId;
                pairingExpiresAt = now + (unsigned long)expiresIn * 1000UL;
                Serial.printf("[Cloud] Pedido de vinculo de %s\n", email.c_str());
                ui_show_pair_confirm(email.c_str(), name.c_str(), expiresIn);
            }
        } else if (announceCode == 403) {
            // Chave diferente da vinculada (ex: aparelho resetado). O dono pode trocar a chave no
            // app/site com o codigo de confirmacao da tela; ate la continua tentando devagar.
            keyRejected = true;
            nextAnnounceAt = now + ANNOUNCE_REJECTED_MS;
            Serial.println("[Cloud] Servidor recusou a chave deste aparelho. No app, vincule de novo com o codigo de confirmacao da tela.");
        } else {
            nextAnnounceAt = now + ANNOUNCE_RETRY_MS;
        }
    }

    // --- Resposta do usuario na tela -> tarefa ---
    if (pendingResponse != -1 && currentPairingId.length()) {
        xSemaphoreTake(cloudMutex, portMAX_DELAY);
        reqRespond = true; reqPairingId = currentPairingId; reqAccept = pendingResponse == 1;
        xSemaphoreGive(cloudMutex);
        xTaskNotifyGive(cloudTask);
        pendingResponse = -1;
        currentPairingId = "";
        return;
    }

    // --- Pedido expirou sem resposta ---
    if (currentPairingId.length() && (long)(now - pairingExpiresAt) > 0) {
        currentPairingId = "";
        ui_hide_pair_confirm();
    }

    // --- Anuncio periodico (ou imediato apos um pair_request) ---
    if (announceInFlight) return;

    // TLS valida a data do certificado: so tenta depois do NTP sincronizar (ano >= 2024)
    if (time(nullptr) < 1704067200) return;
    if (checkRequested || (long)(now - nextAnnounceAt) >= 0) {
        checkRequested = false;
        announceInFlight = true;
        xSemaphoreTake(cloudMutex, portMAX_DELAY);
        reqAnnounce = true; reqLocalIp = WiFi.localIP().toString();
        xSemaphoreGive(cloudMutex);
        xTaskNotifyGive(cloudTask);
    }
}
