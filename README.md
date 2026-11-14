# Breww Controller

Firmware aberto para controlar a temperatura da fermentação de cerveja com um ESP32.
Um sensor DS18B20 mede a cerveja, dois relés ligam o aquecedor e a geladeira, e o aparelho
conversa com o app Breww (iOS e Android) e com [breww.live](https://breww.live).

- Modos fermentação (perfil com passos e rampas, ou alvo manual), geladeira, chope e limpeza
- Proteção do compressor, PID com autoajuste e histerese; alertas no celular pelo app
- Umidade (DHT22, SHT3x, AHT20, BME280) e densímetro iSpindel
- Atualização pelo app, sem cabo

## Instalar sem compilar

Abra **[breww.live/instalar](https://breww.live/instalar)** no Chrome ou no Edge de um computador,
escolha a placa, ligue pelo USB e clique em *Instalar*. Nas placas sem tela, o próprio instalador
pede o Wi-Fi logo depois de gravar. Depois, no app, toque em *Adicionar aparelho*.

## Placas

| Placa | Build | Observação |
|---|---|---|
| Breww com tela 3,5" (ESP32-S3, touch) | `s3-35` (padrão) | Placa oficial, conectores JST |
| ESP32 DevKit (WROOM-32 / V1) | `esp32-devkit` | A mais fácil de achar |
| ESP32-S3-DevKitC | `s3-headless` | Sem tela |
| ESP32-C3 SuperMini | `c3-headless` | Bem pequena e a mais barata |
| ESP8266 (NodeMCU, Wemos D1 mini; 4 MB) | `esp8266-lite` | Versão **Lite** (veja abaixo) |

Os pinos de cada placa estão em [`boards/`](boards). Nas placas sem tela:

- **Relés e I²C** têm pinos fixos (trocáveis pelo app, em *Hardware → Pinos*).
- **DS18B20 e DHT22** podem ir em **qualquer pino livre**: o firmware procura e acha sozinho.
- Opcional: tela OLED/LCD I²C pequena e dois botões para o menu.

O mapa de ligações de cada placa está em [breww.live/instalar](https://breww.live/instalar#pecas).

### Versão Lite (ESP8266)

O ESP8266 tem cerca de 80 KB de RAM, um quarto do ESP32. A versão Lite usa o mesmo código, com a
camada [`platform.h`](platform.h) trocando o que muda de um chip para o outro (configurações em
LittleFS com a mesma interface de `Preferences`, BearSSL no lugar do mbedTLS, sem tarefas em paralelo).

- Funciona igual: controle da fermentação, perfis, app, iSpindel, Wi-Fi pelo instalador e atualização pelo app.
- Uma conexão TLS por vez: durante um HTTPS (anúncio, vínculo, atualização) o MQTT sai e volta em seguida.
  Com o servidor aceitando fragmentos de 1 KB (MFLN), os buffers do TLS caem de ~17 KB para 2 KB.
- Ficam de fora: tela pequena, botões e sensores I²C; volta automática de versão (o ESP8266 não tem
  partição de volta: a imagem nova só é ativada se o MD5 conferir); atualização local pela IDE e pela
  página `/update`; troca de nó em rede mesh.
- Relés no D1 (GPIO 5) e no D2 (GPIO 4), que não mudam de estado no boot; sensores no D5, D6 ou D7.
- NodeMCU com telinha OLED integrada (ex.: HW-364A): sem pinos configurados, o boot procura um OLED
  SSD1306/SH1106 no GPIO14/GPIO12; achando, a tela liga (modo, temperatura, alvo, relé, código de vínculo)
  e o DS18B20 passa para o D7 (GPIO13). O mesmo firmware serve para as duas placas.
- Textos de log ficam na flash (`F()`, `PSTR()`, `printf_P`), senão ocupariam quase 20 KB de RAM.

## Compilar

Com o [arduino-cli](https://arduino.github.io/arduino-cli/) (ou a Arduino IDE 2), usando as versões
fixadas no [workflow de release](.github/workflows/release.yml):

```sh
arduino-cli core install esp32:esp32@3.3.11 \
  --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli lib install "lvgl@8.4.0" "GFX Library for Arduino@1.6.7" "ArduinoJson@7.4.3" \
  "PubSubClient@2.8" "OneWire@2.3.8" "DallasTemperature@4.0.6" "DHT sensor library@1.4.7" \
  "Adafruit Unified Sensor@1.1.15" "AutoPID@1.0.3" "U8g2@2.36.19"
cp ci/lv_conf.h ~/Arduino/libraries/lv_conf.h   # configuração do LVGL (só a placa de tela)

# Placa sem tela: escolha com -DBREWW_BOARD_<NOME>
arduino-cli compile \
  --fqbn "esp32:esp32:esp32:PartitionScheme=min_spiffs" \
  --build-property "compiler.cpp.extra_flags=-DBREWW_BOARD_ESP32_DEVKIT" \
  --build-property "compiler.c.extra_flags=-DBREWW_BOARD_ESP32_DEVKIT" \
  .
```

Outras placas: `-DBREWW_BOARD_S3_HEADLESS` (`esp32:esp32:esp32s3:PartitionScheme=min_spiffs,CDCOnBoot=cdc`),
`-DBREWW_BOARD_C3_HEADLESS` (`esp32:esp32:esp32c3:PartitionScheme=min_spiffs,CDCOnBoot=cdc`) e
`-DBREWW_BOARD_ESP8266_LITE` (`esp8266:esp8266:nodemcuv2:eesz=4M1M`, core `esp8266:esp8266@3.1.2`
de `https://arduino.esp8266.com/stable/package_esp8266com_index.json`).
Sem flag, compila a placa de tela 3,5" (veja o `FQBN` completo no workflow).

## Serial (115200)

Nas placas sem tela, além do protocolo [Improv Serial](https://www.improv-wifi.com/serial/) usado
pelo instalador:

| Comando | O que faz |
|---|---|
| `wifi <rede> <senha>` | Conecta numa rede (`wifi "Minha Rede" senha` para nomes com espaço) |
| `codigo` | Mostra o código de confirmação para vincular no app |
| `status` | Wi-Fi, nuvem e código |
| `pinos` / `pino <ow\|dht\|h\|c\|sda\|scl\|bup\|bdn> <gpio>` / `pinos padrao` | Vê e troca os pinos (`-1` = sem) |
| `rele normal` / `rele invertido` (ou `rele aquecer invertido`…) | Tipo de relé (SSR / módulo comum) |
| `tela sh1106` / `tela ssd1306` | Driver da tela OLED pequena (1,3" / 0,96") |
| `reiniciar` | Reinicia |

## Como o código está organizado

| Arquivo | Papel |
|---|---|
| `breww_controller.ino` | `setup()` e `loop()` |
| `fermentation.*`, `hal_profiles.*` | Controle: perfis, PID, proteção do compressor |
| `hal_sensors.*`, `i2c_sensors.*`, `ispindel.*` | Sensores (e a detecção automática de pinos) |
| `hal_relays.*` | Relés e polaridade |
| `net_manager.*`, `cloud_pairing.*` | Wi-Fi, MQTT com TLS, vínculo com a conta, atualização |
| `wifi_setup.*`, `wifi_portal.*`, `improv_serial.*` | Configuração do Wi-Fi |
| `board_pins.*`, `boards/` | Pinos por placa, validados por chip |
| `ui.cpp` | Interface da placa de tela (LVGL) |
| `ui_headless.cpp`, `small_display.*`, `button_menu.*` | Placas sem tela: LED, serial, tela pequena e botões |

## Contribuir

Veja [CONTRIBUTING.md](CONTRIBUTING.md). Falhas de segurança: [SECURITY.md](SECURITY.md), nunca em issue pública.

## Licença e marca

O código é livre, sob a **GNU General Public License v3.0 ou posterior** ([LICENSE](LICENSE)).
Você pode usar, estudar, modificar e distribuir, inclusive vendendo placas, desde que entregue
o código-fonte das suas versões sob a mesma licença.

O nome **Breww**, o logotipo e a identidade visual **não** fazem parte da licença: são marcas
do projeto. Versões modificadas devem usar outro nome. Bibliotecas e fontes de terceiros
mantêm as próprias licenças ([NOTICE.md](NOTICE.md)).
