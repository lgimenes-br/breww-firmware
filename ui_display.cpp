// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// Display e touch das placas com tela grande (LVGL 8). As telas ficam em ui.cpp.
//  - s3-35: painel AXS15231B 320x480 por QSPI (Arduino_GFX), desenhado em paisagem 480x320; touch I2C 0x3B.
//  - p4-7:  painel JD9165 MIPI-DSI 1024x600 pelo driver DO FABRICANTE (src/p4_lcd, sem alteracao),
//           desenhado em resolucao nativa; touch GT911 no I2C 7/8.
#include "config.h"
#if BREWW_HAS_LVGL
#include <Arduino.h>
#include <lvgl.h>
#include <Wire.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "esp_heap_caps.h"
#include "ui_display.h"

#if defined(BREWW_PANEL_DSI)
#include "src/p4_lcd/jd9165_lcd.h"
#include "esp_lcd_mipi_dsi.h"

static jd9165_lcd p4lcd(PANEL_LCD_RST);
static lv_disp_drv_t *p4_pending = NULL; // desenho em andamento (a copia por DMA termina na interrupcao)
static uint8_t gt911_addr = 0;

// A copia para a memoria do painel e assincrona (DMA2D): o LVGL so reaproveita o buffer quando ela termina.
static bool IRAM_ATTR p4_on_copy_done(esp_lcd_panel_handle_t, esp_lcd_dpi_panel_event_data_t *, void *) {
  if (p4_pending) { lv_disp_flush_ready(p4_pending); p4_pending = NULL; }
  return false;
}

// O painel e iniciado num processo do NUCLEO 0, para as interrupcoes dele ficarem nesse nucleo. O loop
// (nucleo 1) le o DHT22 e o DS18B20 com as interrupcoes desligadas por alguns ms; com o painel no mesmo
// nucleo, a tela piscava (azul) a cada leitura.
static void p4_panel_start_task(void *arg) {
  p4lcd.begin();  // inicializacao, tempos e luz do fabricante
  xTaskNotifyGive((TaskHandle_t)arg);
  vTaskDelete(NULL);
}

static bool p4_async = false;
static void p4_panel_begin() {
  xTaskCreatePinnedToCore(p4_panel_start_task, "p4_panel", 6144, xTaskGetCurrentTaskHandle(), 5, NULL, 0);
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000));
  bsp_lcd_handles_t h;
  p4lcd.get_handle(&h);
  esp_lcd_dpi_panel_event_callbacks_t cbs = {};
  cbs.on_color_trans_done = p4_on_copy_done;
  p4_async = esp_lcd_dpi_panel_register_event_callbacks(h.panel, &cbs, NULL) == ESP_OK;
}

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  if (p4_async) p4_pending = drv; // liberado quando a copia terminar
  p4lcd.lcd_draw_bitmap(a->x1, a->y1, a->x2 + 1, a->y2 + 1, (uint16_t *)&px->full);
  if (!p4_async) lv_disp_flush_ready(drv);
}

static bool gt911_read(uint16_t reg, uint8_t *b, size_t n) {
  Wire.beginTransmission(gt911_addr);
  Wire.write(reg >> 8); Wire.write(reg & 0xFF);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom((int)gt911_addr, (int)n) != (int)n) return false;
  for (size_t i = 0; i < n; i++) b[i] = Wire.read();
  return true;
}
static void gt911_clear() {
  Wire.beginTransmission(gt911_addr);
  Wire.write(0x81); Wire.write(0x4E); Wire.write(0x00);
  Wire.endTransmission();
}

static void touch_read(lv_indev_drv_t *, lv_indev_data_t *data) {
  static int16_t last_x = 0, last_y = 0;
  static bool pressed = false;
  if (!gt911_addr) {
    static uint32_t lastProbe = 0;
    if (millis() - lastProbe > 2000) {
      lastProbe = millis();
      for (uint8_t a : {0x5D, 0x14}) { Wire.beginTransmission(a); if (Wire.endTransmission() == 0) { gt911_addr = a; break; } }
      if (gt911_addr) Serial.printf("[Touch] GT911 em 0x%02X\n", gt911_addr);
    }
  } else {
    uint8_t st = 0;
    if (gt911_read(0x814E, &st, 1) && (st & 0x80)) {
      if ((st & 0x0F) > 0) {
        uint8_t p[4];
        if (gt911_read(0x8150, p, 4)) {
          last_x = constrain((int)(p[0] | (p[1] << 8)), 0, SCREEN_WIDTH - 1);
          last_y = constrain((int)(p[2] | (p[3] << 8)), 0, SCREEN_HEIGHT - 1);
        }
        pressed = true;
      } else {
        pressed = false;
      }
      gt911_clear();
    }
  }
  data->state = pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
  data->point.x = last_x;
  data->point.y = last_y;
}

#else // s3-35
#include <Arduino_GFX_Library.h>
#define TOUCH_ADDR 0x3B

static Arduino_DataBus *bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
static Arduino_GFX *panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
static Arduino_Canvas *gfx = new Arduino_Canvas(320, 480, panel, 0, 0, 1);
static const uint8_t req_cmd[8] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08};
static SemaphoreHandle_t flush_sem;

// O envio do quadro inteiro (canvas -> painel) roda no nucleo 0. O LVGL so desenha o proximo quadro no
// canvas depois que este envio termina: antes, os dois corriam juntos e o painel recebia metade de um
// quadro e metade do outro (animacoes "aos trancos").
static lv_disp_drv_t *flush_drv = NULL;
static void flush_task(void *) {
  for (;;) if (xSemaphoreTake(flush_sem, portMAX_DELAY) == pdTRUE) {
    gfx->flush();
    if (flush_drv) lv_disp_flush_ready(flush_drv);
  }
}

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *a, lv_color_t *px) {
  gfx->draw16bitRGBBitmap(a->x1, a->y1, (uint16_t *)&px->full, a->x2 - a->x1 + 1, a->y2 - a->y1 + 1);
  if (lv_disp_flush_is_last(drv)) { flush_drv = drv; xSemaphoreGive(flush_sem); } // pronto quando o envio acabar
  else lv_disp_flush_ready(drv);
}

static void touch_read(lv_indev_drv_t *, lv_indev_data_t *data) {
  static uint32_t last_pressed = 0;
  static int16_t last_x = 0, last_y = 0;
  static bool is_pressed = false;
  data->state = LV_INDEV_STATE_REL;
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(req_cmd, 8);
  Wire.endTransmission();
  if (Wire.requestFrom(TOUCH_ADDR, 8) == 8) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = Wire.read();
    bool now = false;
    if (b[1] != 0xFF && b[1] != 0x00) {
      const uint16_t rx = ((b[2] & 0x0F) << 8) | b[3], ry = ((b[4] & 0x0F) << 8) | b[5];
      if (rx < 320 && ry < 480) { last_x = ry; last_y = 320 - rx; now = true; }
    }
    // Filtro anti-ruido do touch capacitivo
    if (now) { is_pressed = true; last_pressed = millis(); }
    else if (millis() - last_pressed > 60) is_pressed = false;
    data->state = is_pressed ? LV_INDEV_STATE_PR : LV_INDEV_STATE_REL;
    data->point.x = last_x;
    data->point.y = last_y;
  }
}
#endif // BREWW_PANEL_DSI

void ui_display_begin() {
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000); // le o touch mais rapido
#if defined(BREWW_PANEL_DSI)
  p4_panel_begin(); // driver do fabricante (inclui a luz de fundo)
#else
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  flush_sem = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(flush_task, "flush_task", 4096, NULL, 1, NULL, 0);
  gfx->begin();
#endif

  lv_init();

  static lv_disp_draw_buf_t draw_buf;
#if defined(BREWW_PANEL_DSI)
  // 1024x600: dois buffers de 100 linhas na PSRAM (o LVGL desenha um enquanto o DMA copia o outro)
  const uint32_t n = SCREEN_WIDTH * 100;
  lv_color_t *b1 = (lv_color_t *)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_color_t *b2 = (lv_color_t *)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
  lv_disp_draw_buf_init(&draw_buf, b1, b2, n);
#else
  // 1/5 da tela na RAM INTERNA: a PSRAM e lenta para o desenho pixel a pixel (fontes, bordas)
  uint32_t n = SCREEN_WIDTH * SCREEN_HEIGHT / 5;
  lv_color_t *b1 = (lv_color_t *)heap_caps_malloc(n * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!b1) { n = SCREEN_WIDTH * SCREEN_HEIGHT / 10; b1 = (lv_color_t *)malloc(n * sizeof(lv_color_t)); }
  lv_disp_draw_buf_init(&draw_buf, b1, NULL, n);
#endif

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = disp_flush;
  disp_drv.draw_buf = &draw_buf;
  lv_disp_t *disp = lv_disp_drv_register(&disp_drv);
  if (disp && disp->refr_timer) lv_timer_set_period(disp->refr_timer, 10); // ate ~100 quadros/s

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = touch_read;
  indev_drv.scroll_limit = 5; // rolagem responde antes (padrao 10)
  lv_indev_t *indev = lv_indev_drv_register(&indev_drv);
  if (indev && indev->driver && indev->driver->read_timer) lv_timer_set_period(indev->driver->read_timer, 15);
}
#endif // BREWW_HAS_LVGL
