// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "hal_sensors.h"
#include "board_pins.h"
#include "i2c_sensors.h"

// Instancias do OneWire e DallasTemperature (pinos vem de board_pins, carregados no boot)
OneWire oneWire;
DallasTemperature sensors(&oneWire);
static DHT* dht = nullptr; // criado no init_sensors (sem DHT configurado = nullptr)

unsigned long lastTempRequest = 0;

// ---------------------------------------------------------------------------
// Mapeamento DS18B20 por MAC (definido pelo app via "set_sensors")
//   slot 0 -> currentTemp  (Sensor 1 / controle, "ferm")
//   slot 1 -> currentTemp2 (Sensor 2 / geladeira, "amb")
// Sem mapeamento salvo, usa a ordem do barramento (comportamento antigo).
// ---------------------------------------------------------------------------
#define MAX_DS18B20 5
String sensorMacCtrl = "";
String sensorMac2 = "";

static DeviceAddress sensorAddresses[MAX_DS18B20];
static String sensorHex[MAX_DS18B20];
static int sensorSlot[MAX_DS18B20];
static int cachedDeviceCount = -1;
static bool mappingDirty = true;

String ds18b20_addr_to_hex(const DeviceAddress addr) {
    char buf[17];
    for (int i = 0; i < 8; i++) sprintf(buf + i * 2, "%02X", addr[i]);
    buf[16] = '\0';
    return String(buf);
}

static String normalize_mac(String mac) {
    mac.trim();
    mac.toUpperCase();
    mac.replace(":", "");
    mac.replace("-", "");
    if (mac.length() != 16) return "";
    for (unsigned int i = 0; i < mac.length(); i++) {
        if (!isxdigit(mac[i])) return "";
    }
    return mac;
}

// Decide qual sensor fisico alimenta cada slot (currentTemp / currentTemp2)
static void assign_slots(int count) {
    bool hasMapping = sensorMacCtrl.length() > 0 || sensorMac2.length() > 0;
    bool slotUsed[2] = {false, false};
    for (int i = 0; i < count; i++) sensorSlot[i] = -1;

    if (!hasMapping) {
        for (int i = 0; i < count && i < 2; i++) sensorSlot[i] = i;
        return;
    }
    // 1a passada: sensores mapeados explicitamente
    for (int i = 0; i < count; i++) {
        if (sensorMacCtrl.length() && sensorHex[i] == sensorMacCtrl) { sensorSlot[i] = 0; slotUsed[0] = true; }
        else if (sensorMac2.length() && sensorHex[i] == sensorMac2) { sensorSlot[i] = 1; slotUsed[1] = true; }
    }
    // 2a passada: sensores nao mapeados so ocupam slots SEM MAC configurado
    for (int i = 0; i < count; i++) {
        if (sensorSlot[i] != -1) continue;
        if (!slotUsed[0] && sensorMacCtrl.length() == 0) { sensorSlot[i] = 0; slotUsed[0] = true; }
        else if (!slotUsed[1] && sensorMac2.length() == 0) { sensorSlot[i] = 1; slotUsed[1] = true; }
    }
}

void set_sensor_mapping(const String& macCtrl, const String& mac2) {
    sensorMacCtrl = normalize_mac(macCtrl);
    sensorMac2 = normalize_mac(mac2);
    if (sensorMac2 == sensorMacCtrl) sensorMac2 = "";

    Preferences p;
    p.begin("breww", false);
    p.putString("mac_ctrl", sensorMacCtrl);
    p.putString("mac_2", sensorMac2);
    p.end();

    // Reinicia a media movel para nao misturar leituras de sensores diferentes
    currentTemp = 0.0;
    currentTemp2 = 0.0;
    mappingDirty = true;
    Serial.printf_P(PSTR("[Sensores] Mapeamento salvo -> Controle: %s | Sensor 2: %s\n"),
                  sensorMacCtrl.length() ? sensorMacCtrl.c_str() : "(auto)",
                  sensorMac2.length() ? sensorMac2.c_str() : "(auto)");
}

int scan_ds18b20(String macs[], float temps[], int maxCount) {
    sensors.begin(); // re-enumera o barramento (pega sensores plugados a quente)
    int count = sensors.getDeviceCount();
    if (count > maxCount) count = maxCount;

    DeviceAddress addr;
    sensors.setWaitForConversion(true);  // leitura fresca e bloqueante (~750ms, so no scan)
    sensors.requestTemperatures();
    sensors.setWaitForConversion(false);
    for (int i = 0; i < count; i++) {
        if (sensors.getAddress(addr, i)) {
            macs[i] = ds18b20_addr_to_hex(addr);
            float t = sensors.getTempC(addr);
            temps[i] = (t == DEVICE_DISCONNECTED_C) ? NAN : t;
        } else {
            macs[i] = "";
            temps[i] = NAN;
        }
    }
    cachedDeviceCount = -1; // forca refazer a cache de enderecos no proximo ciclo
    sensors.requestTemperatures();
    lastTempRequest = millis();
    Serial.printf_P(PSTR("[Sensores] Scan: %d DS18B20 encontrados\n"), count);
    return count;
}


unsigned long lastDhtRequest = 0;
const int DHT_REQUEST_INTERVAL = 2500; // DHT22 eh lento, ler a cada 2.5s
const unsigned long DHT_STALE_MS = 10000; // Sem leitura valida por 10s => umidade invalida (NAN)
unsigned long lastDhtOkMs = 0;
unsigned long dhtFailCount = 0;

void init_sensors() {
    Serial.println(F("Inicializando barramento de sensores 1-Wire..."));
    oneWire.begin(g_pins.oneWire);
    pinMode(g_pins.oneWire, INPUT_PULLUP);
    sensors.begin();
    // Acelera a leitura (resolucao de 12 bits eh mais estavel e o delay assincrono compensa)
    sensors.setResolution(12); 
    sensors.setWaitForConversion(false); // Nao travar o codigo esperando o sensor ler
    sensors.requestTemperatures();
    if (g_pins.dht >= 0) { dht = new DHT(g_pins.dht, DHTTYPE); dht->begin(); }
    lastTempRequest = millis();

    Preferences p;
    p.begin("breww", true);
    sensorMacCtrl = p.getString("mac_ctrl", "");
    sensorMac2 = p.getString("mac_2", "");
    p.end();
    mappingDirty = true;
}

// ---------------------------------------------------------------------------
// Plug&play dos sensores (placas sem tela): reles e I2C tem pino fixo; o DS18B20 e o DHT22
// podem ir em qualquer pino livre e seguro (pins_sensor_candidate). Sem sensor no pino salvo,
// procura nos outros a cada 30 s e grava onde achou (o app mostra em Hardware).
// Os testes sao pulsos curtos (0,5 a 1 ms): nao dao tempo de um rele mecanico fechar.
// Placa de tela: pinos fixos, mas confere se o DS18B20 e o DHT22 estao invertidos no JST 4P.
// ---------------------------------------------------------------------------
static bool probe_ds18b20(int pin) {
    OneWire w(pin);
    uint8_t a[8];
    w.reset_search();
    bool found = w.search(a) && OneWire::crc8(a, 7) == a[7] &&
                 (a[0] == 0x28 || a[0] == 0x10 || a[0] == 0x22 || a[0] == 0x3B); // familias DS18x20
    pinMode(pin, INPUT);
    return found;
}

#if defined(ESP8266)
#define PROBE_ENTER() noInterrupts()
#define PROBE_EXIT() interrupts()
#else
static portMUX_TYPE probeMux = portMUX_INITIALIZER_UNLOCKED;
#define PROBE_ENTER() portENTER_CRITICAL(&probeMux)
#define PROBE_EXIT() portEXIT_CRITICAL(&probeMux)
#endif

// Duracao do nivel atual (us) ou -1 se passar do limite
static int32_t level_for(int pin, int level, uint32_t limitUs) {
    uint32_t start = micros();
    while (digitalRead(pin) == level) if (micros() - start > limitUs) return -1;
    return (int32_t)(micros() - start);
}

/** Leitura completa do DHT22 com checksum: so um DHT de verdade passa. */
static bool probe_dht22(int pin) {
    pinMode(pin, INPUT_PULLUP);
    delayMicroseconds(50);
    if (digitalRead(pin) == LOW) { pinMode(pin, INPUT); return false; } // linha presa em baixo
    uint8_t d[5] = {0};
    bool ok = true;
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
    delayMicroseconds(1100);
    pinMode(pin, INPUT_PULLUP); // fora da secao critica (como a biblioteca DHT)
    delayMicroseconds(55);
    PROBE_ENTER();
    if (level_for(pin, LOW, 200) < 0 || level_for(pin, HIGH, 200) < 0) ok = false; // resposta ~80/80 us
    for (int i = 0; ok && i < 40; i++) {
        int32_t lo = level_for(pin, LOW, 200);
        int32_t hi = lo < 0 ? -1 : level_for(pin, HIGH, 200);
        if (hi < 0) { ok = false; break; }
        d[i / 8] = (d[i / 8] << 1) | (hi > lo ? 1 : 0);
    }
    PROBE_EXIT();
    pinMode(pin, INPUT);
    if (!ok || d[4] != (uint8_t)(d[0] + d[1] + d[2] + d[3])) return false;
    const int hum = (d[0] << 8) | d[1];
    return hum > 0 && hum <= 1000; // 0,1 a 100%
}

static void use_onewire_pin(int p) {
    g_pins.oneWire = p;
    oneWire.begin(p);
    pinMode(p, INPUT_PULLUP);
    sensors.begin();
    sensors.setResolution(12);
    sensors.setWaitForConversion(false);
    sensors.requestTemperatures();
    lastTempRequest = millis();
    cachedDeviceCount = -1;
}

static void use_dht_pin(int p) {
    delete dht;
    dht = nullptr;
    g_pins.dht = p;
    if (p >= 0) { dht = new DHT(p, DHTTYPE); dht->begin(); }
    lastDhtRequest = millis();
    dhtFailCount = 0;
}

static void autodetect_sensors() {
    const bool needOw = sensors.getDeviceCount() == 0;
    const bool dhtOk = dht && lastDhtOkMs && millis() - lastDhtOkMs < DHT_STALE_MS;
    bool needDht = !dhtOk && !i2c_humidity_active();
    if (!needOw && !needDht) return;

#if BREWW_HAS_LVGL
    // Pinos fixos: so testa a inversao entre os dois fios de dados do JST 4P
    const int ow = g_pins.oneWire, dh = g_pins.dht;
    if (ow < 0 || dh < 0 || !needOw) return;
    if ((!dhtOk && probe_ds18b20(dh)) || (needDht && probe_dht22(ow))) {
        Serial.printf_P(PSTR("[Sensores] DS18B20 e DHT22 invertidos no conector: DS18B20 agora no GPIO %d, DHT22 no %d\n"), dh, ow);
        use_dht_pin(ow);
        use_onewire_pin(dh);
        pins_set_sensor_swap(g_pins.oneWire != PIN_ONE_WIRE_BUS);
    }
#else

    bool changed = false;
    for (int p = 0; p <= PINS_MAX_GPIO && (sensors.getDeviceCount() == 0 || needDht); p++) {
        if (!pins_sensor_candidate(p) || p == g_pins.oneWire) continue;
        if (p == g_pins.dht && dhtOk) continue;
        if (sensors.getDeviceCount() == 0 && probe_ds18b20(p)) {
            Serial.printf_P(PSTR("[Sensores] DS18B20 encontrado no GPIO %d (antes: %d)\n"), p, g_pins.oneWire);
            if (p == g_pins.dht) use_dht_pin(-1);
            use_onewire_pin(p);
            changed = true;
            continue;
        }
        if (needDht && probe_dht22(p)) {
            Serial.printf_P(PSTR("[Sensores] DHT22 encontrado no GPIO %d (antes: %d)\n"), p, g_pins.dht);
            use_dht_pin(p);
            changed = true;
            needDht = false; // um DHT basta
        }
    }
    if (changed) {
        String err;
        if (!pins_save(g_pins, err)) Serial.printf_P(PSTR("[Sensores] Nao gravou os pinos achados: %s\n"), err.c_str());
    }
#endif
}

static unsigned long lastValidMs[2] = {0, 0}; // ultima leitura valida por slot (sensor_fresh)

void read_temperatures() {
    static unsigned long lastAutodetect = 0;
    // Primeira busca depois de 4 s (DHT22 precisa de ~2 s apos ligar); depois a cada 30 s enquanto faltar sensor
    // (depois de 10 min ligado, a cada 5 min: quem nao tem DHT nao fica levando pulso a toa)
    const unsigned long every = millis() < 600000UL ? 30000UL : 300000UL;
    if (millis() > 4000 && (lastAutodetect == 0 || millis() - lastAutodetect > every)) {
        lastAutodetect = millis();
        autodetect_sensors();
    }
    if (millis() - lastTempRequest >= 1000) {
        // Tenta achar o sensor novamente caso tenha sido plugado com a placa ja ligada
        if(sensors.getDeviceCount() == 0) {
            sensors.begin();
        }
        
        int deviceCount = sensors.getDeviceCount();
        if (deviceCount > MAX_DS18B20) deviceCount = MAX_DS18B20;
        
        // Cache dos enderecos 1-Wire para evitar lentidao extrema do LVGL (Touch travando)
        // A funcao getTempCByIndex faz uma varredura completa no barramento a cada chamada,
        // o que bloqueia o processador por ~20ms e faz o touch e as animacoes engasgarem!
        // Atualiza a cache apenas se o numero de sensores mudar (hot-plug) ou apos um scan
        if (deviceCount != cachedDeviceCount) {
            for (int i = 0; i < deviceCount; i++) {
                sensors.getAddress(sensorAddresses[i], i);
                sensorHex[i] = ds18b20_addr_to_hex(sensorAddresses[i]);
            }
            cachedDeviceCount = deviceCount;
            mappingDirty = true;
        }
        if (mappingDirty) {
            assign_slots(deviceCount);
            mappingDirty = false;
        }
        
        // Agora, lemos instantaneamente direto da memoria (< 1ms por sensor)
        for (int i = 0; i < deviceCount; i++) {
            int slot = sensorSlot[i];
            if (slot < 0) continue;
            float temp = sensors.getTempC(sensorAddresses[i]);
            if (temp > -50.0 && temp < 85.0 && temp != DEVICE_DISCONNECTED_C) {
                lastValidMs[slot == 0 ? 0 : 1] = millis();
                if (slot == 0) {
                    if (currentTemp == 0.0) currentTemp = temp;
                    else currentTemp = (currentTemp * 0.8) + (temp * 0.2);
                }
                else if (slot == 1) {
                    if (currentTemp2 == 0.0) currentTemp2 = temp;
                    else currentTemp2 = (currentTemp2 * 0.8) + (temp * 0.2);
                }
            }
        }
        
        // Print serial a cada 5 segs para n flodar
        static unsigned long lastPrint = 0;
        static int lastPrinted = -1;
        if (deviceCount != lastPrinted || millis() - lastPrint > 60000) { // so na mudanca (ou 1x por minuto)
            Serial.printf_P(PSTR("[Sensores] DS18B20 (Cache ativos): %d\n"), deviceCount);
            lastPrint = millis();
            lastPrinted = deviceCount;
        }
        
        // Solicita nova leitura do DS18B20 para o proximo ciclo de forma assincrona
        sensors.requestTemperatures();
        lastTempRequest = millis();
    }
    
    // Leitura Autodetectavel do DHT22 em um ciclo independente (mais lento)
    i2c_sensors_loop(); // SHT3x/AHT20/BME280: quando presente, fornece a umidade
    // DHT22 e opcional: se nunca respondeu neste pino (lastDhtOkMs == 0), para de ler depois de 3
    // tentativas e avisa uma vez. A busca automatica (a cada 30 s) acha um DHT22 ligado depois e o
    // use_dht_pin zera a contagem. Ja funcionou e falha agora: segue lendo e avisando (defeito real).
    const bool dhtNeverOk = lastDhtOkMs == 0;
    if (dht && dhtNeverOk && dhtFailCount >= 3) {
        static int8_t notedPin = -2;
        if (notedPin != g_pins.dht) {
            notedPin = g_pins.dht;
            Serial.printf_P(PSTR("[DHT22] Nenhum DHT22 no pino %d (e opcional). Procurando nos pinos livres a cada 30 s.\n"), g_pins.dht);
        }
    } else if (dht && !i2c_humidity_active() && millis() - lastDhtRequest >= DHT_REQUEST_INTERVAL) {
        float h = dht->readHumidity();
        float t = dht->readTemperature();
        if (!isnan(h) && !isnan(t)) {
            currentHumidity = h;
            lastDhtOkMs = millis();
            dhtFailCount = 0;
        } else {
            dhtFailCount++;
            // Nunca reportar um valor velho/falso: apos 10s sem leitura valida, invalida a umidade
            if (lastDhtOkMs == 0 || millis() - lastDhtOkMs > DHT_STALE_MS) {
                currentHumidity = NAN;
            }
        }
        lastDhtRequest = millis();

        static unsigned long lastDhtPrint = 0;
        if (millis() - lastDhtPrint > 5000) {
            if (dhtFailCount == 0) {
                Serial.printf_P(PSTR("[DHT22] OK %.1f%% / %.1fC (pino %d)\n"), h, t, g_pins.dht);
            } else if (!dhtNeverOk) {
                Serial.printf_P(PSTR("[DHT22] FALHA: %lu leituras seguidas sem resposta (pino %d). Verifique fiacao DATA, 3.3V/5V, GND e pull-up 10k.\n"),
                              dhtFailCount, g_pins.dht);
            }
            lastDhtPrint = millis();
        }
    }
}

bool sensor_fresh(int slot) {
    if (slot < 0 || slot > 1 || !lastValidMs[slot]) return false;
    return millis() - lastValidMs[slot] < SENSOR_STALE_MS;
}

int ds18b20_count() { return cachedDeviceCount < 0 ? 0 : cachedDeviceCount; }
