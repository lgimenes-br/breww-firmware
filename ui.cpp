// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
#include "config.h"
#if BREWW_HAS_LVGL // tela grande (placa s3-35); sem ela, ui_headless.cpp
#include <lvgl.h>
#include <Preferences.h>
#include "ui.h"

LV_FONT_DECLARE(lv_font_montserrat_extrabold_48);



#include "config.h"
#include "globals.h"
#include "net_manager.h"
#include <WiFi.h>
#include "cloud_pairing.h"
#include "ispindel.h"
#include "hal_profiles.h"
#include "device_settings.h"
#include "fermentation.h"
#include "wifi_setup.h"


// --- Globais de UI ---
static lv_obj_t * arc_temp = NULL;
static lv_obj_t * lbl_temp = NULL;
static lv_obj_t * lbl_gravity = NULL;
static lv_obj_t * lbl_setpoint = NULL;
static lv_obj_t * lbl_time_left = NULL;
lv_obj_t * lbl_chopp_temp = NULL;
lv_obj_t * lbl_chopp_hum = NULL;
lv_obj_t * lbl_chopp_setpoint = NULL;
lv_obj_t * lbl_chopp_setpoint_unit = NULL;
lv_obj_t * lbl_chopp_name = NULL;
lv_obj_t * lbl_geladeira_s1 = NULL;
lv_obj_t * lbl_geladeira_s2 = NULL;
lv_obj_t * lbl_geladeira_hum = NULL;
lv_obj_t * lbl_geladeira_uptime = NULL;
lv_obj_t * lbl_telemetry_temp = NULL;
lv_obj_t * lbl_telemetry_temp_unit = NULL;
lv_obj_t * lbl_telemetry_t2_unit = NULL;
lv_obj_t * lbl_telemetry_setpoint_unit = NULL;
lv_obj_t * lbl_chopp_temp_unit = NULL;
lv_obj_t * lbl_geladeira_s2_unit = NULL;
lv_obj_t * lbl_geladeira_sp = NULL;
lv_obj_t * lbl_geladeira_sp_unit = NULL;
lv_obj_t * lbl_telemetry_sg = NULL;
lv_obj_t * lbl_telemetry_t2 = NULL;
lv_obj_t * lbl_telemetry_hum = NULL;
lv_obj_t * lbl_telemetry_setpoint = NULL;
lv_obj_t * lbl_telemetry_fg = NULL;
lv_obj_t * lbl_telemetry_heater = NULL;
lv_obj_t * lbl_telemetry_cooler = NULL;
lv_obj_t * lbl_telemetry_pwm = NULL;
lv_obj_t * lbl_telemetry_uptime = NULL;
lv_obj_t * lbl_telemetry_ram = NULL;
lv_obj_t * lbl_pid_kp = NULL;
lv_obj_t * lbl_pid_ki = NULL;
lv_obj_t * lbl_pid_kd = NULL;
lv_obj_t * lbl_pid_pwm = NULL;
lv_obj_t * btn_mode_ferm = NULL;
lv_obj_t * btn_mode_geladeira = NULL;
lv_obj_t * btn_mode_chopp = NULL;
lv_obj_t * btn_mode_clean = NULL;


// ==========================================
// TODA A LOGICA E LAYOUT DO LVGL (MIGRADA)
// ==========================================
#include <Wire.h>
#include <Arduino_GFX_Library.h>
#include <lvgl.h>
#include <AutoPID.h>

lv_obj_t * ispindel_chart = NULL;
lv_chart_series_t * ser_ispindel = NULL;

void ui_update_ispindel_chart() {
    if (ispindel_chart == NULL || ser_ispindel == NULL) return;
    
    lv_chart_set_all_value(ispindel_chart, ser_ispindel, LV_CHART_POINT_NONE);
    
    int chart_index = 0;
    for(int i=0; i<ispindelHistoryCount; i++) {
        int idx = (ispindelHistoryIndex - ispindelHistoryCount + i + ISPINDEL_HISTORY_SIZE) % ISPINDEL_HISTORY_SIZE;
        float val = ispindelHistory[idx];
        if (val > 0) {
            lv_chart_set_value_by_id(ispindel_chart, ser_ispindel, chart_index, (lv_coord_t)(val * 1000.0));
            chart_index++;
        }
    }
    lv_chart_refresh(ispindel_chart);
}

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#include "logo_img.h"

 


#define TOUCH_ADDR 0x3B 




Arduino_DataBus *bus = new Arduino_ESP32QSPI(45, 47, 21, 48, 40, 39);
Arduino_GFX *panel = new Arduino_AXS15231B(bus, GFX_NOT_DEFINED, 0, false, 320, 480);
Arduino_Canvas *gfx = new Arduino_Canvas(320, 480, panel, 0, 0, 1);

const uint8_t req_cmd[8] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x08};

static lv_disp_draw_buf_t draw_buf;
static lv_color_t *buf;

SemaphoreHandle_t flush_sem;

void flush_task(void *pvParameters) {
    while(1) {
        if (xSemaphoreTake(flush_sem, portMAX_DELAY) == pdTRUE) {
            gfx->flush();
        }
    }
}

void my_disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p) {
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
  
  if (lv_disp_flush_is_last(disp_drv)) {
      xSemaphoreGive(flush_sem);
  }
  
  lv_disp_flush_ready(disp_drv);
}

void my_touchpad_read(lv_indev_drv_t *indev_drv, lv_indev_data_t *data) {
  static uint32_t last_pressed = 0;
  static int16_t last_x = 0;
  static int16_t last_y = 0;
  static bool is_pressed = false;

  data->state = LV_INDEV_STATE_REL;
  Wire.beginTransmission(TOUCH_ADDR);
  Wire.write(req_cmd, 8);
  Wire.endTransmission();
  
  if (Wire.requestFrom(TOUCH_ADDR, 8) == 8) {
    uint8_t buffer[8];
    for (int i = 0; i < 8; i++) buffer[i] = Wire.read();
    
    bool current_pressed = false;
    if (buffer[1] != 0xFF && buffer[1] != 0x00) {
      uint16_t raw_x = ((buffer[2] & 0x0F) << 8) | buffer[3];
      uint16_t raw_y = ((buffer[4] & 0x0F) << 8) | buffer[5];
      if (raw_x < 320 && raw_y < 480) {
        last_x = raw_y;
        last_y = 320 - raw_x; 
        current_pressed = true;
      }
    }
    
    // Filtro anti-ruído (debouncing do touch capacitivo)
    if (current_pressed) {
        is_pressed = true;
        last_pressed = millis();
    } else {
        if (millis() - last_pressed > 60) {
            is_pressed = false;
        }
    }
    
    if (is_pressed) {
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = last_x;
    data->point.y = last_y;
  }
}

// --- DECLARAÇÕES DAS TELAS E CABEÇALHOS ---
lv_obj_t * scr_splash;
lv_obj_t * scr_wiz_welcome;
lv_obj_t * scr_wiz_wifi;
lv_obj_t * wifi_list = NULL;
lv_obj_t * scr_wiz_pass;
lv_obj_t * scr_wiz_connecting;
lv_obj_t * lbl_connecting = NULL;
lv_obj_t * lbl_connecting_ssid = NULL;
bool is_bypassing_wizard = false;
bool force_ui_update = false;
String selected_ssid = "";
lv_obj_t * scr_wiz_pair;
lv_obj_t * scr_wiz_ready;
lv_obj_t * scr_launcher;
lv_obj_t * content_view;

lv_obj_t * header_back_btn; 
lv_obj_t * header_close_menu_btn;
lv_obj_t * badge;
lv_obj_t * clock_txt;
lv_obj_t * dots;

// Declarações dos Apps
void build_app_grid();
void build_app_dashboard();
void build_app_telemetry();
void build_app_fermentation();
void build_app_logs();
void build_app_settings();
void build_app_about();

void open_app(int app_id);
void btn_back_event_cb(lv_event_t * e);

// --- ROTEADORES DINÂMICOS DE TELA ---
static void btn_close_menu_cb(lv_event_t * e) {
  if (currentMode == MODE_FERM) {
      if (isRampActive) open_app(2);
      else btn_back_event_cb(NULL);
  } else if (currentMode == MODE_GELADEIRA) {
      open_app(3); // App 3 = Geladeira
  } else if (currentMode == MODE_CHOPP) {
      open_app(0); // App 0 = Choppeira
  } else if (currentMode == MODE_CLEAN) {
      btn_back_event_cb(NULL);
  }
}

void btn_back_event_cb(lv_event_t * e) {
  lv_obj_clean(content_view);
  arc_temp = NULL;
  lbl_temp = NULL;
  lbl_gravity = NULL;
  lbl_setpoint = NULL;
  
  lbl_time_left = NULL;
  lbl_chopp_temp = NULL;
  lbl_chopp_hum = NULL;
  lbl_chopp_setpoint = NULL;
  lbl_chopp_setpoint_unit = NULL;
  lbl_chopp_name = NULL;
  lbl_geladeira_s1 = NULL;
  lbl_geladeira_s2 = NULL;
  lbl_geladeira_hum = NULL;
  lbl_geladeira_uptime = NULL;
  lbl_telemetry_temp_unit = NULL;
  lbl_telemetry_t2_unit = NULL;
  lbl_telemetry_setpoint_unit = NULL;
  lbl_chopp_temp_unit = NULL;
  lbl_geladeira_s2_unit = NULL;
  lbl_geladeira_sp = NULL;
  lbl_geladeira_sp_unit = NULL;
  lbl_telemetry_temp = NULL;
  lbl_telemetry_sg = NULL;
  lbl_telemetry_t2 = NULL;
  lbl_telemetry_hum = NULL;
  lbl_telemetry_setpoint = NULL;
  lbl_telemetry_fg = NULL;
  lbl_telemetry_heater = NULL;
  lbl_telemetry_cooler = NULL;
  lbl_telemetry_pwm = NULL;
  lbl_telemetry_uptime = NULL;
  lbl_telemetry_ram = NULL;

  
  lv_obj_add_flag(header_back_btn, LV_OBJ_FLAG_HIDDEN); 
  lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN); 
  
  if (currentMode == 0 && !isRampActive) {
      if(header_close_menu_btn != NULL) lv_obj_add_flag(header_close_menu_btn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -20, 20);
  } else {
      if(header_close_menu_btn != NULL) lv_obj_clear_flag(header_close_menu_btn, LV_OBJ_FLAG_HIDDEN);
      lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -65, 20);
  }
  lv_obj_align(clock_txt, LV_ALIGN_TOP_MID, 0, 18); 

  build_app_grid();
}


void btn_activate_mode_cb(lv_event_t * e) {
    int appMode = (int)(uintptr_t)lv_event_get_user_data(e);
    settings_set_mode(appMode);
    
    if (currentMode == 0) open_app(2);
    else if (currentMode == 1) open_app(3);
    else if (currentMode == 2) open_app(0);
}

void open_app(int app_id) {
  force_ui_update = true;
  lv_obj_clean(content_view);
  arc_temp = NULL;
  lbl_temp = NULL;
  lbl_gravity = NULL;
  lbl_setpoint = NULL;
  
  lbl_time_left = NULL;
  lbl_chopp_temp = NULL;
  lbl_chopp_hum = NULL;
  lbl_chopp_setpoint = NULL;
  lbl_chopp_setpoint_unit = NULL;
  lbl_chopp_name = NULL;
  lbl_geladeira_s1 = NULL;
  lbl_geladeira_s2 = NULL;
  lbl_geladeira_hum = NULL;
  lbl_geladeira_uptime = NULL;
  lbl_telemetry_temp_unit = NULL;
  lbl_telemetry_t2_unit = NULL;
  lbl_telemetry_setpoint_unit = NULL;
  lbl_chopp_temp_unit = NULL;
  lbl_geladeira_s2_unit = NULL;
  lbl_geladeira_sp = NULL;
  lbl_geladeira_sp_unit = NULL;
  lbl_telemetry_temp = NULL;
  lbl_telemetry_sg = NULL;
  lbl_telemetry_t2 = NULL;
  lbl_telemetry_hum = NULL;
  lbl_telemetry_setpoint = NULL;
  lbl_telemetry_fg = NULL;
  lbl_telemetry_heater = NULL;
  lbl_telemetry_cooler = NULL;
  lbl_telemetry_pwm = NULL;
  lbl_telemetry_uptime = NULL;
  lbl_telemetry_ram = NULL;
  lbl_pid_kp = NULL;
  lbl_pid_ki = NULL;
  lbl_pid_kd = NULL;
  lbl_pid_pwm = NULL;

  lv_obj_clear_flag(header_back_btn, LV_OBJ_FLAG_HIDDEN);
  lv_obj_clear_flag(dots, LV_OBJ_FLAG_HIDDEN);
  if(header_close_menu_btn != NULL) lv_obj_add_flag(header_close_menu_btn, LV_OBJ_FLAG_HIDDEN);

  lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -65, 20);
  lv_obj_align(clock_txt, LV_ALIGN_TOP_MID, 0, 18);
  
  lv_obj_clear_flag(content_view, LV_OBJ_FLAG_SCROLLABLE);

  switch(app_id) {
    case 0: build_app_dashboard(); break;
    case 1: build_app_telemetry(); break;
    case 2: build_app_fermentation(); break;
    case 3: build_app_logs(); break;
case 4: build_app_settings(); break;
    case 5: build_app_about(); break;
  }
  
  // App -> modo que ele representa (mesmos ids da grade do launcher: 2 = FERMENTADOR).
  // Telemetria (1) nao e um modo: nunca mostra "ATIVAR MODO".
  int appMode = -1;
  if (app_id == 0) appMode = MODE_CHOPP;
  else if (app_id == 2) appMode = MODE_FERM;
  else if (app_id == 3) appMode = MODE_GELADEIRA;
  
  if (appMode != -1 && appMode != currentMode) {
      lv_obj_t * overlay = lv_obj_create(content_view);
      lv_obj_set_size(overlay, 450, 70);
      lv_obj_align(overlay, LV_ALIGN_BOTTOM_MID, 0, 0);
      lv_obj_set_style_bg_color(overlay, lv_color_hex(0x1a1a1a), 0);
      lv_obj_set_style_bg_opa(overlay, LV_OPA_90, 0);
      lv_obj_set_style_border_color(overlay, lv_color_hex(0x333333), 0);
      lv_obj_set_style_border_width(overlay, 1, 0);
      lv_obj_set_style_radius(overlay, 0, 0);
      lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
      
      lv_obj_t * btn = lv_btn_create(overlay);
      lv_obj_set_size(btn, 200, 44);
      lv_obj_center(btn);
      lv_obj_set_style_bg_color(btn, lv_color_hex(0x10b981), 0);
      lv_obj_set_style_radius(btn, 8, 0);
      lv_obj_add_event_cb(btn, btn_activate_mode_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)appMode);
      
      lv_obj_t * lbl = lv_label_create(btn);
      lv_label_set_text(lbl, "ATIVAR MODO");
      lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
      lv_obj_center(lbl);
  }
}

void btn_app_event_cb(lv_event_t * e) {
  int app_id = (int)(uintptr_t)lv_event_get_user_data(e);
  open_app(app_id);
}

void btn_start_cb(lv_event_t * e) { start_wifi_scan(); lv_scr_load_anim(scr_wiz_wifi, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); }
void list_wifi_cb(lv_event_t * e) { lv_scr_load_anim(scr_wiz_pair, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); }
void btn_pair_cb(lv_event_t * e) { lv_scr_load_anim(scr_wiz_ready, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); }
void btn_finish_cb(lv_event_t * e) { lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_NONE, 0, 0, false); }

void add_wiz_header(lv_obj_t * parent) {
  lv_obj_t * logo = lv_img_create(parent);
  lv_img_set_src(logo, &img_breww_header);
  lv_obj_align(logo, LV_ALIGN_TOP_LEFT, 15, 15);
}

// ----------------------------------------------------
// TELA 1: SPLASH GIGANTE
// ----------------------------------------------------
void build_splash() {
  scr_splash = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_splash, lv_color_hex(0x050505), 0);
  
  lv_obj_t * logo = lv_img_create(scr_splash);
  lv_img_set_src(logo, &img_breww_splash);
  lv_img_set_zoom(logo, 256); // 256 = 100% scale (no pixelation) 
  lv_obj_align(logo, LV_ALIGN_CENTER, 0, -20);
  
  lv_obj_t * sub = lv_label_create(scr_splash);
  lv_label_set_text(sub, "V2.4.0-STABLE");
  lv_obj_set_style_text_font(sub, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(sub, lv_color_hex(0x333333), 0);
  lv_obj_set_style_text_letter_space(sub, 2, 0);
  lv_obj_align(sub, LV_ALIGN_BOTTOM_MID, 0, -30);
}

// ----------------------------------------------------
// WIZARD
// ----------------------------------------------------
void build_wiz_welcome() {
  scr_wiz_welcome = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_wiz_welcome, lv_color_hex(0x050505), 0);
  lv_obj_t * title = lv_label_create(scr_wiz_welcome);
  lv_label_set_text(title, "Bem-vindo ao Breww");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -50);
  lv_obj_t * desc = lv_label_create(scr_wiz_welcome);
  lv_label_set_text(desc, "Seu equipamento de automacao cervejeira\nde alta performance.");
  lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(desc, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_align(desc, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(desc, LV_ALIGN_CENTER, 0, -10);
  lv_obj_t * btn = lv_btn_create(scr_wiz_welcome);
  lv_obj_set_size(btn, 160, 44);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 50);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(btn, 22, 0);
  lv_obj_add_event_cb(btn, btn_start_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t * lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "COMECAR " LV_SYMBOL_RIGHT);
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
  lv_obj_center(lbl);
}



static void wiz_pass_back_cb(lv_event_t * e) {
    lv_scr_load_anim(scr_wiz_wifi, LV_SCR_LOAD_ANIM_NONE, 150, 0, false);
}

static void kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * kb = lv_event_get_target(e);
    if(code == LV_EVENT_READY) {
        lv_obj_t * ta = lv_keyboard_get_textarea(kb);
        String pass = lv_textarea_get_text(ta);
        wifi_setup_connect_new(selected_ssid, pass); // grava, conecta e acompanha (wifi_setup.cpp)
        is_bypassing_wizard = false;
        if(scr_wiz_connecting == NULL) {
                      scr_wiz_connecting = lv_obj_create(NULL);
          lv_obj_set_style_bg_color(scr_wiz_connecting, lv_color_hex(0x050505), 0);
          
          lbl_connecting = lv_label_create(scr_wiz_connecting);
          lv_label_set_text(lbl_connecting, "Conectando ao Wi-Fi");
          lv_obj_set_style_text_font(lbl_connecting, &lv_font_montserrat_24, 0);
          lv_obj_set_style_text_color(lbl_connecting, lv_color_hex(0xFFFFFF), 0);
          lv_obj_align(lbl_connecting, LV_ALIGN_CENTER, 0, -20);
          
          lbl_connecting_ssid = lv_label_create(scr_wiz_connecting);
          lv_label_set_text(lbl_connecting_ssid, "");
          lv_obj_set_style_text_font(lbl_connecting_ssid, &lv_font_montserrat_12, 0);
          lv_obj_set_style_text_color(lbl_connecting_ssid, lv_color_hex(0x666666), 0);
          lv_obj_align(lbl_connecting_ssid, LV_ALIGN_CENTER, 0, 10);
        }
        lv_label_set_text(lbl_connecting, "Conectando ao Wi-Fi");
        lv_obj_set_style_text_color(lbl_connecting, lv_color_hex(0xFFFFFF), 0);
        if(lbl_connecting_ssid) lv_label_set_text(lbl_connecting_ssid, selected_ssid.c_str());
        lv_scr_load_anim(scr_wiz_connecting, LV_SCR_LOAD_ANIM_NONE, 150, 0, false);

    } else if(code == LV_EVENT_CANCEL) {
        lv_scr_load_anim(scr_wiz_wifi, LV_SCR_LOAD_ANIM_NONE, 150, 0, false);
    }
}

static void wifi_network_clicked_cb(lv_event_t * e) {
    lv_obj_t * btn = lv_event_get_target(e);
    selected_ssid = lv_list_get_btn_text(wifi_list, btn);
    
    // Create Pass Screen on the fly if not exists
    if(scr_wiz_pass == NULL) {
        scr_wiz_pass = lv_obj_create(NULL);
        lv_obj_set_style_bg_color(scr_wiz_pass, lv_color_hex(0x050505), 0);
        
        lv_obj_t * title = lv_label_create(scr_wiz_pass);
        lv_label_set_text(title, "Digite a Senha");
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 30);
        
        lv_obj_t * back_btn = lv_btn_create(scr_wiz_pass);
        lv_obj_set_size(back_btn, 50, 40);
        lv_obj_align(back_btn, LV_ALIGN_TOP_LEFT, 10, 10);
        lv_obj_set_style_bg_opa(back_btn, 0, 0);
        lv_obj_set_style_shadow_width(back_btn, 0, 0);
        lv_obj_t * back_lbl = lv_label_create(back_btn);
        lv_label_set_text(back_lbl, LV_SYMBOL_LEFT);
        lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(back_lbl, lv_color_hex(0xFFFFFF), 0);
        lv_obj_center(back_lbl);
        lv_obj_add_event_cb(back_btn, wiz_pass_back_cb, LV_EVENT_CLICKED, NULL);
        
        lv_obj_t * ta = lv_textarea_create(scr_wiz_pass);
        lv_textarea_set_password_mode(ta, true);
        lv_textarea_set_one_line(ta, true);
        lv_obj_set_size(ta, 400, 40);
        lv_obj_align(ta, LV_ALIGN_TOP_MID, 0, 70);
        
        lv_obj_t * kb = lv_keyboard_create(scr_wiz_pass);
        lv_obj_set_size(kb, SCREEN_WIDTH, 220);
        lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
        lv_keyboard_set_textarea(kb, ta);
        
        // Estilo Main (Fundo)
        lv_obj_set_style_bg_color(kb, lv_color_hex(0x000000), LV_PART_MAIN);
        lv_obj_set_style_border_width(kb, 0, LV_PART_MAIN);
        lv_obj_set_style_pad_all(kb, 4, LV_PART_MAIN);
        lv_obj_set_style_pad_row(kb, 8, LV_PART_MAIN);
        lv_obj_set_style_pad_column(kb, 8, LV_PART_MAIN);
        
        // Estilo Botoes
        lv_obj_set_style_bg_color(kb, lv_color_hex(0x1a1a1a), LV_PART_ITEMS);
        lv_obj_set_style_text_color(kb, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
        lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
        lv_obj_set_style_border_width(kb, 0, LV_PART_ITEMS);
        lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);

        lv_obj_add_event_cb(kb, kb_event_cb, LV_EVENT_ALL, NULL);
    }
    
    lv_scr_load_anim(scr_wiz_pass, LV_SCR_LOAD_ANIM_NONE, 150, 0, false);
}


lv_obj_t * wifi_loading_lbl = NULL;

static void build_dynamic_wifi_list(int num_networks) {
    if(wifi_loading_lbl != NULL) {
        lv_obj_del(wifi_loading_lbl);
        wifi_loading_lbl = NULL;
    }
    
    wifi_list = lv_list_create(scr_wiz_wifi);
    lv_obj_set_size(wifi_list, 300, 160);
    lv_obj_align(wifi_list, LV_ALIGN_TOP_MID, 0, 120);
    lv_obj_set_style_bg_color(wifi_list, lv_color_hex(0x050505), 0);
    lv_obj_set_style_border_width(wifi_list, 0, 0);
    
    String added_ssids = "";
    for(int i = 0; i < num_networks; i++) {
        String ssid = get_scanned_ssid(i);
        if (added_ssids.indexOf("[" + ssid + "]") == -1) {
            added_ssids += "[" + ssid + "]";
            lv_obj_t * btn = lv_list_add_btn(wifi_list, LV_SYMBOL_WIFI, ssid.c_str());
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x1A1A1A), 0);
        lv_obj_set_style_text_color(btn, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_radius(btn, 8, 0);
        lv_obj_add_event_cb(btn, wifi_network_clicked_cb, LV_EVENT_CLICKED, NULL);
        }
    }
}

void check_wifi_scan_task(lv_timer_t * timer) {
    if(lv_scr_act() != scr_wiz_wifi) return;
    if(wifi_list != NULL) return; // Ja populado
    
    int n = check_wifi_scan();
    if(n > 0) {
        build_dynamic_wifi_list(n);
    } else if (n == 0) {
        // Fallback se n achar nada, procura denovo
        start_wifi_scan();
    }
}

void build_wiz_wifi() {
  scr_wiz_wifi = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_wiz_wifi, lv_color_hex(0x050505), 0);
  lv_obj_t * title = lv_label_create(scr_wiz_wifi);
  lv_label_set_text(title, "Conecte-se ao Wi-Fi");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 60);
  
  wifi_loading_lbl = lv_label_create(scr_wiz_wifi);
  lv_label_set_text(wifi_loading_lbl, "Buscando redes...");
  lv_obj_set_style_text_color(wifi_loading_lbl, lv_color_hex(0x888888), 0);
  lv_obj_align(wifi_loading_lbl, LV_ALIGN_CENTER, 0, 0);
  
  // Timer para verificar o scan
  lv_timer_create(check_wifi_scan_task, 500, NULL);
}
// Codigo de confirmacao: so existe depois que a chave nasce (Wi-Fi ligado). O timer completa
// a tela (texto + QR com &code=) assim que ele estiver disponivel.
static lv_obj_t * pair_qr = NULL;
static lv_obj_t * pair_confirm_txt = NULL;
static lv_timer_t * pair_code_timer = NULL;

static void pair_code_refresh(lv_timer_t * t) {
  String code;
  if (!pair_qr || !pair_confirm_txt || !cloud_pair_code(code)) return;
  const String url = "https://breww.live/?pair=" + get_device_id() + "&code=" + code;
  lv_qrcode_update(pair_qr, url.c_str(), url.length());
  lv_label_set_text(pair_confirm_txt, code.c_str());
  lv_timer_del(t);
  pair_code_timer = NULL;
}

static void pair_screen_deleted(lv_event_t * e) {
  // Tela apagada: o timer nao pode mais tocar nos objetos dela
  pair_qr = NULL;
  pair_confirm_txt = NULL;
  if (pair_code_timer) { lv_timer_del(pair_code_timer); pair_code_timer = NULL; }
}

void build_wiz_pair() {
  const String deviceId = get_device_id();
  const String pairUrl = "https://breww.live/?pair=" + deviceId;

  scr_wiz_pair = lv_obj_create(NULL);
  lv_obj_add_event_cb(scr_wiz_pair, pair_screen_deleted, LV_EVENT_DELETE, NULL);
  lv_obj_set_style_bg_color(scr_wiz_pair, lv_color_hex(0x050505), 0);
  lv_obj_t * title = lv_label_create(scr_wiz_pair);
  lv_label_set_text(title, "Vincule seu Equipamento");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 20);
  lv_obj_t * desc = lv_label_create(scr_wiz_pair);
  lv_label_set_text(desc, "Escaneie o QR code com o celular ou digite o codigo em breww.live");
  lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(desc, lv_color_hex(0x888888), 0);
  lv_obj_align(desc, LV_ALIGN_TOP_MID, 0, 56);

  // QR code (fundo branco + margem para leitura confiavel em tela escura)
  lv_obj_t * qr_bg = lv_obj_create(scr_wiz_pair);
  lv_obj_set_size(qr_bg, 156, 156);
  lv_obj_align(qr_bg, LV_ALIGN_LEFT_MID, 40, 8);
  lv_obj_set_style_bg_color(qr_bg, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_border_width(qr_bg, 0, 0);
  lv_obj_set_style_radius(qr_bg, 8, 0);
  lv_obj_set_style_pad_all(qr_bg, 0, 0);
  lv_obj_clear_flag(qr_bg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t * qr = lv_qrcode_create(qr_bg, 140, lv_color_hex(0x000000), lv_color_hex(0xFFFFFF));
  lv_qrcode_update(qr, pairUrl.c_str(), pairUrl.length());
  lv_obj_center(qr);
  pair_qr = qr;

  // Codigo para digitar manualmente
  lv_obj_t * code_caption = lv_label_create(scr_wiz_pair);
  lv_label_set_text(code_caption, "CODIGO DO APARELHO");
  lv_obj_set_style_text_font(code_caption, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(code_caption, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_letter_space(code_caption, 2, 0);
  lv_obj_align(code_caption, LV_ALIGN_RIGHT_MID, -62, -34);
  lv_obj_t * code_box = lv_obj_create(scr_wiz_pair);
  lv_obj_set_size(code_box, 230, 56);
  lv_obj_align(code_box, LV_ALIGN_RIGHT_MID, -20, 8);
  lv_obj_set_style_bg_color(code_box, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(code_box, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(code_box, 1, 0);
  lv_obj_set_style_radius(code_box, 12, 0);
  lv_obj_set_style_pad_all(code_box, 0, 0);
  lv_obj_clear_flag(code_box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_t * code_txt = lv_label_create(code_box);
  lv_label_set_text(code_txt, deviceId.c_str());
  lv_obj_set_style_text_font(code_txt, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(code_txt, lv_color_hex(0x10b981), 0);
  lv_obj_center(code_txt);

  // Codigo de confirmacao (prova que e este aparelho; vai tambem no QR)
  lv_obj_t * confirm_caption = lv_label_create(scr_wiz_pair);
  lv_label_set_text(confirm_caption, "CODIGO DE CONFIRMACAO");
  lv_obj_set_style_text_font(confirm_caption, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(confirm_caption, lv_color_hex(0x888888), 0);
  lv_obj_set_style_text_letter_space(confirm_caption, 2, 0);
  lv_obj_align(confirm_caption, LV_ALIGN_RIGHT_MID, -50, 50);
  pair_confirm_txt = lv_label_create(scr_wiz_pair);
  lv_label_set_text(pair_confirm_txt, "aguardando Wi-Fi");
  lv_obj_set_style_text_font(pair_confirm_txt, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(pair_confirm_txt, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_letter_space(pair_confirm_txt, 3, 0);
  lv_obj_align(pair_confirm_txt, LV_ALIGN_RIGHT_MID, -82, 72);
  if (!pair_code_timer) pair_code_timer = lv_timer_create(pair_code_refresh, 1000, NULL);
  pair_code_refresh(pair_code_timer);

  lv_obj_t * btn = lv_btn_create(scr_wiz_pair);
  lv_obj_set_size(btn, 160, 44);
  lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, 0, -14);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(btn, 22, 0);
  lv_obj_add_event_cb(btn, btn_pair_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t * lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "VINCULAR");
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
  lv_obj_center(lbl);
}

void build_wiz_ready() {
  scr_wiz_ready = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_wiz_ready, lv_color_hex(0x050505), 0);
  
  lv_obj_t * icon = lv_label_create(scr_wiz_ready);
  lv_label_set_text(icon, LV_SYMBOL_OK);
  lv_obj_set_style_text_font(icon, &lv_font_montserrat_extrabold_48, 0);
  lv_obj_set_style_text_color(icon, lv_color_hex(0x10b981), 0);
  lv_obj_align(icon, LV_ALIGN_CENTER, 0, -65);
  
  lv_obj_t * title = lv_label_create(scr_wiz_ready);
  lv_label_set_text(title, "Tudo Pronto!");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_CENTER, 0, -10);
  
  lv_obj_t * desc = lv_label_create(scr_wiz_ready);
  // Removido o acento para evitar quadradinho de caractere nao mapeado
  lv_label_set_text(desc, "Seu Breww esta configurado e online.");
  lv_obj_set_style_text_font(desc, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(desc, lv_color_hex(0x888888), 0);
  lv_obj_align(desc, LV_ALIGN_CENTER, 0, 15);
  
  lv_obj_t * btn = lv_btn_create(scr_wiz_ready);
  lv_obj_set_size(btn, 160, 44);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 65);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(btn, 22, 0);
  lv_obj_add_event_cb(btn, btn_finish_cb, LV_EVENT_CLICKED, NULL);
  
  lv_obj_t * lbl = lv_label_create(btn);
  lv_label_set_text(lbl, "CONCLUIR");
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0x000000), 0);
  lv_obj_center(lbl);
}



lv_obj_t * kb_fg = NULL;
lv_obj_t * ta_fg = NULL;
lv_obj_t * scr_fg = NULL;

static void fg_kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY) {
        String val = lv_textarea_get_text(ta_fg);
        settings_set_final_gravity(val.toFloat());
        
        lv_obj_del(scr_fg);
        scr_fg = NULL;
    } else if(code == LV_EVENT_CANCEL) {
        lv_obj_del(scr_fg);
        scr_fg = NULL;
    }
}


// --- PID Keyboard Logic ---
lv_obj_t * kb_pid = NULL;
lv_obj_t * ta_pid = NULL;
lv_obj_t * scr_pid = NULL;
int current_pid_editing = 0; // 1=kp, 2=ki, 3=kd, 4=pwm


static void update_mode_ui() {
    if(!btn_mode_ferm) return;
    lv_obj_set_style_bg_color(btn_mode_ferm, lv_color_hex(currentMode == MODE_FERM ? 0xf59e0b : 0x222222), 0);
    lv_obj_set_style_bg_color(btn_mode_geladeira, lv_color_hex(currentMode == MODE_GELADEIRA ? 0x8b5cf6 : 0x222222), 0);
    lv_obj_set_style_bg_color(btn_mode_chopp, lv_color_hex(currentMode == MODE_CHOPP ? 0x3b82f6 : 0x222222), 0);
    lv_obj_set_style_bg_color(btn_mode_clean, lv_color_hex(currentMode == MODE_CLEAN ? 0x10b981 : 0x222222), 0);
}

static void mode_btn_event_cb(lv_event_t * e) {
    int m = (int)(uintptr_t)lv_event_get_user_data(e);
    settings_set_mode(m);
    update_mode_ui();
}


static void autotune_btn_event_cb(lv_event_t * e) {
    extern void start_autotune();
    start_autotune();
}

static void pid_kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY) {
        String val = lv_textarea_get_text(ta_pid);
        if (current_pid_editing == 1) {
            settings_set_pid_gains(val.toDouble(), pidKi, pidKd);
            if(lbl_pid_kp) lv_label_set_text(lbl_pid_kp, val.c_str());
        } else if (current_pid_editing == 2) {
            settings_set_pid_gains(pidKp, val.toDouble(), pidKd);
            if(lbl_pid_ki) lv_label_set_text(lbl_pid_ki, val.c_str());
        } else if (current_pid_editing == 3) {
            settings_set_pid_gains(pidKp, pidKi, val.toDouble());
            if(lbl_pid_kd) lv_label_set_text(lbl_pid_kd, val.c_str());
        } else if (current_pid_editing == 4) {
            settings_set_pwm_window(val.toInt());
            if(lbl_pid_pwm) lv_label_set_text(lbl_pid_pwm, val.c_str());
        }
        
        lv_obj_del(scr_pid);
        scr_pid = NULL;
    } else if(code == LV_EVENT_CANCEL) {
        lv_obj_del(scr_pid);
        scr_pid = NULL;
    }
}

static void open_pid_kb(int mode, const char* title, const char* current_val) {
    if(scr_pid != NULL) return;
    current_pid_editing = mode;
    
    scr_pid = lv_obj_create(lv_scr_act()); 
    lv_obj_set_size(scr_pid, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(scr_pid, 0, 0);
    lv_obj_set_style_bg_color(scr_pid, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr_pid, 200, 0);
    lv_obj_set_style_border_width(scr_pid, 0, 0);
    
    lv_obj_t * kb_panel = lv_obj_create(scr_pid);
    lv_obj_set_size(kb_panel, 300, 280);
    lv_obj_center(kb_panel);
    lv_obj_set_style_bg_color(kb_panel, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(kb_panel, lv_color_hex(0x333333), 0);
    
    lv_obj_t * l = lv_label_create(kb_panel);
    lv_label_set_text(l, title);
    lv_obj_set_style_text_color(l, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 0);
    
    ta_pid = lv_textarea_create(kb_panel);
    lv_textarea_set_one_line(ta_pid, true);
    lv_obj_set_size(ta_pid, 200, 40);
    lv_obj_align(ta_pid, LV_ALIGN_TOP_MID, 0, 30);
    lv_textarea_set_text(ta_pid, current_val);
    
    kb_pid = lv_keyboard_create(kb_panel);
    lv_keyboard_set_mode(kb_pid, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb_pid, 280, 160);
    lv_obj_align(kb_pid, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb_pid, ta_pid);
    
    lv_obj_add_event_cb(kb_pid, pid_kb_event_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb_pid, pid_kb_event_cb, LV_EVENT_CANCEL, NULL);
}

static void kp_click_cb(lv_event_t * e) { char buf[16]; sprintf(buf, "%.1f", pidKp); open_pid_kb(1, "Novo Kp", buf); }
static void ki_click_cb(lv_event_t * e) { char buf[16]; sprintf(buf, "%.1f", pidKi); open_pid_kb(2, "Novo Ki", buf); }
static void kd_click_cb(lv_event_t * e) { char buf[16]; sprintf(buf, "%.1f", pidKd); open_pid_kb(3, "Novo Kd", buf); }
static void pwm_click_cb(lv_event_t * e) { char buf[16]; sprintf(buf, "%d", pwmWindow); open_pid_kb(4, "Janela PWM (ms)", buf); }

static void fg_click_cb(lv_event_t * e) {
    if(scr_fg != NULL) return;
    
    scr_fg = lv_obj_create(lv_scr_act()); 
    lv_obj_set_size(scr_fg, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(scr_fg, 0, 0);
    lv_obj_set_style_bg_color(scr_fg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr_fg, 200, 0); 
    lv_obj_set_style_border_width(scr_fg, 0, 0);
    lv_obj_set_style_pad_all(scr_fg, 0, 0);
    lv_obj_set_style_radius(scr_fg, 0, 0);
    lv_obj_clear_flag(scr_fg, LV_OBJ_FLAG_SCROLLABLE);
    
    ta_fg = lv_textarea_create(scr_fg);
    lv_textarea_set_one_line(ta_fg, true);
    lv_obj_set_size(ta_fg, 200, 50);
    lv_obj_align(ta_fg, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(ta_fg, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_color(ta_fg, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(ta_fg, 1, 0);
    lv_obj_set_style_text_color(ta_fg, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ta_fg, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ta_fg, LV_TEXT_ALIGN_CENTER, 0);
    char buf[10];
    sprintf(buf, "%.3f", finalGravity);
    lv_textarea_set_text(ta_fg, buf);
    
    kb_fg = lv_keyboard_create(scr_fg);
    lv_keyboard_set_mode(kb_fg, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb_fg, SCREEN_WIDTH, 240);
    lv_obj_align(kb_fg, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb_fg, ta_fg);
    
    lv_obj_set_style_bg_color(kb_fg, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(kb_fg, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb_fg, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(kb_fg, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb_fg, 8, LV_PART_MAIN);
    
    lv_obj_set_style_bg_color(kb_fg, lv_color_hex(0x1a1a1a), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb_fg, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_radius(kb_fg, 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb_fg, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb_fg, 0, LV_PART_ITEMS);

    lv_obj_add_event_cb(kb_fg, fg_kb_event_cb, LV_EVENT_ALL, NULL);
}

lv_obj_t * kb_setpoint = NULL;
lv_obj_t * ta_setpoint = NULL;
lv_obj_t * scr_setpoint = NULL;

static void setpoint_kb_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    if(code == LV_EVENT_READY) {
        String val = lv_textarea_get_text(ta_setpoint);
        settings_set_setpoint(val.toFloat()); // publica na hora; grava com debounce
        
        lv_obj_del(scr_setpoint);
        scr_setpoint = NULL;
    } else if(code == LV_EVENT_CANCEL) {
        lv_obj_del(scr_setpoint);
        scr_setpoint = NULL;
    }
}

static void setpoint_click_cb(lv_event_t * e) {
    if(scr_setpoint != NULL) return;
    
    scr_setpoint = lv_obj_create(lv_scr_act()); 
    lv_obj_set_size(scr_setpoint, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(scr_setpoint, 0, 0);
    lv_obj_set_style_bg_color(scr_setpoint, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr_setpoint, 200, 0); 
    lv_obj_set_style_border_width(scr_setpoint, 0, 0);
    lv_obj_set_style_pad_all(scr_setpoint, 0, 0);
    lv_obj_set_style_radius(scr_setpoint, 0, 0);
    lv_obj_clear_flag(scr_setpoint, LV_OBJ_FLAG_SCROLLABLE);
    
    ta_setpoint = lv_textarea_create(scr_setpoint);
    lv_textarea_set_one_line(ta_setpoint, true);
    lv_obj_set_size(ta_setpoint, 200, 50);
    lv_obj_align(ta_setpoint, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(ta_setpoint, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_color(ta_setpoint, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(ta_setpoint, 1, 0);
    lv_obj_set_style_text_color(ta_setpoint, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ta_setpoint, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(ta_setpoint, LV_TEXT_ALIGN_CENTER, 0);
    char buf[10];
    sprintf(buf, "%.1f", currentSetpoint);
    lv_textarea_set_text(ta_setpoint, buf);
    
    kb_setpoint = lv_keyboard_create(scr_setpoint);
    lv_keyboard_set_mode(kb_setpoint, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(kb_setpoint, SCREEN_WIDTH, 240);
    lv_obj_align(kb_setpoint, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb_setpoint, ta_setpoint);
    
    // Estilo Main (Fundo)
    lv_obj_set_style_bg_color(kb_setpoint, lv_color_hex(0x000000), LV_PART_MAIN);
    lv_obj_set_style_border_width(kb_setpoint, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(kb_setpoint, 4, LV_PART_MAIN);
    lv_obj_set_style_pad_row(kb_setpoint, 8, LV_PART_MAIN);
    lv_obj_set_style_pad_column(kb_setpoint, 8, LV_PART_MAIN);
    
    // Estilo Botoes
    lv_obj_set_style_bg_color(kb_setpoint, lv_color_hex(0x1a1a1a), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb_setpoint, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
    lv_obj_set_style_radius(kb_setpoint, 6, LV_PART_ITEMS);
    lv_obj_set_style_border_width(kb_setpoint, 0, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb_setpoint, 0, LV_PART_ITEMS);

    lv_obj_add_event_cb(kb_setpoint, setpoint_kb_event_cb, LV_EVENT_ALL, NULL);
}

// ----------------------------------------------------
// APP 0: DASHBOARD
// ----------------------------------------------------
lv_obj_t * create_right_card(lv_obj_t * parent, int y, const char * title, const char * val, const char * unit, const char * icon_symbol, uint32_t color, uint32_t bg_color) {
  lv_obj_t * card = lv_obj_create(parent);
  lv_obj_set_size(card, 190, 75); 
  lv_obj_align(card, LV_ALIGN_TOP_RIGHT, 0, y);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 12, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_pad_all(card, 12, 0);

  lv_obj_t * t = lv_label_create(card);
  lv_label_set_text(t, title);
  lv_obj_set_style_text_font(t, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(t, lv_color_hex(0x888888), 0);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, 0, 0);

  lv_obj_t * icon_box = lv_obj_create(card);
  lv_obj_set_size(icon_box, 36, 36);
  lv_obj_align(icon_box, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(icon_box, lv_color_hex(bg_color), 0);
  lv_obj_set_style_border_width(icon_box, 0, 0);
  lv_obj_set_style_radius(icon_box, LV_RADIUS_CIRCLE, 0);
  lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t * icn = lv_label_create(icon_box);
  lv_label_set_text(icn, icon_symbol);
  lv_obj_set_style_text_color(icn, lv_color_hex(color), 0);
  lv_obj_center(icn);

  lv_obj_t * v = lv_label_create(card);
  lv_label_set_text(v, val);
  lv_obj_set_style_text_font(v, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(v, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align_to(v, icon_box, LV_ALIGN_OUT_RIGHT_MID, 12, 0);

  lv_obj_t * u = lv_label_create(card);
  lv_label_set_text(u, unit);
  lv_obj_set_style_text_font(u, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(u, lv_color_hex(0x666666), 0);
  lv_obj_align_to(u, v, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -3);
  return v;
}

// APP 0: CHOPPEIRA (Ex-Dashboard)
// ----------------------------------------------------
lv_obj_t * scr_chopp_edit = NULL;
lv_obj_t * ta_name = NULL;
lv_obj_t * ta_type = NULL;
lv_obj_t * ta_ibu = NULL;
lv_obj_t * ta_abv = NULL;
lv_obj_t * kb_chopp = NULL;
lv_obj_t * form_cont = NULL;

static void form_ta_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t * ta = lv_event_get_target(e);
    
    if(code == LV_EVENT_CLICKED || code == LV_EVENT_FOCUSED) {
        if(kb_chopp != NULL) {
            lv_keyboard_set_textarea(kb_chopp, ta);
            lv_obj_clear_flag(kb_chopp, LV_OBJ_FLAG_HIDDEN);
            
            if (ta == ta_ibu || ta == ta_abv) {
                lv_keyboard_set_mode(kb_chopp, LV_KEYBOARD_MODE_NUMBER);
            } else {
                lv_keyboard_set_mode(kb_chopp, LV_KEYBOARD_MODE_TEXT_UPPER);
            }
            
            if (form_cont != NULL) {
                if (ta == ta_name) lv_obj_set_y(form_cont, 0);
                else if (ta == ta_type) lv_obj_set_y(form_cont, -60);
                else if (ta == ta_ibu) lv_obj_set_y(form_cont, -110);
                else if (ta == ta_abv) lv_obj_set_y(form_cont, -160);
            }
        }
    } else if(code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        if(kb_chopp != NULL) lv_obj_add_flag(kb_chopp, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(ta, LV_STATE_FOCUSED);
        if (form_cont != NULL) lv_obj_set_y(form_cont, 0);
    }
}

static void chopp_save_cb(lv_event_t * e) {
    settings_set_beer_info(lv_textarea_get_text(ta_name), lv_textarea_get_text(ta_type),
                           String(lv_textarea_get_text(ta_ibu)).toInt(),
                           String(lv_textarea_get_text(ta_abv)).toFloat()); // grava e publica
    
    if (lbl_chopp_name != NULL) {
        lv_label_set_text(lbl_chopp_name, beerName.c_str());
    }
    
    force_ui_update = true; // force the stats to redraw if they depend on loop_ui (but they are static in build_app_dashboard)
    
    // Actually, stats are statically drawn in build_app_dashboard, so we must redraw the whole dashboard!
    open_app(0);
    
    lv_obj_del(scr_chopp_edit);
    scr_chopp_edit = NULL;
}

static void chopp_cancel_cb(lv_event_t * e) {
    lv_obj_del(scr_chopp_edit);
    scr_chopp_edit = NULL;
}

static lv_obj_t * create_form_row(lv_obj_t * parent, const char * label_text, lv_obj_t ** ta_out, const char * init_val, int y_pos) {
    lv_obj_t * lbl = lv_label_create(parent);
    lv_label_set_text(lbl, label_text);
    lv_obj_set_style_text_color(lbl, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_LEFT, 20, y_pos + 12);
    
    lv_obj_t * ta = lv_textarea_create(parent);
    lv_textarea_set_one_line(ta, true);
    lv_obj_set_size(ta, 240, 40);
    lv_obj_align(ta, LV_ALIGN_TOP_RIGHT, -20, y_pos);
    lv_obj_set_style_bg_color(ta, lv_color_hex(0x1a1a1a), 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(0x333333), 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
    lv_textarea_set_text(ta, init_val);
    lv_obj_add_event_cb(ta, form_ta_event_cb, LV_EVENT_ALL, NULL);
    
    *ta_out = ta;
    return ta;
}

static void chopp_edit_click_cb(lv_event_t * e) {
    if(scr_chopp_edit != NULL) return;
    
    scr_chopp_edit = lv_obj_create(lv_scr_act()); 
    lv_obj_set_size(scr_chopp_edit, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(scr_chopp_edit, 0, 0);
    lv_obj_set_style_bg_color(scr_chopp_edit, lv_color_hex(0x0a0a0a), 0);
    lv_obj_set_style_border_width(scr_chopp_edit, 0, 0);
    lv_obj_clear_flag(scr_chopp_edit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(scr_chopp_edit, LV_SCROLLBAR_MODE_OFF);
    
    form_cont = lv_obj_create(scr_chopp_edit);
    lv_obj_set_size(form_cont, SCREEN_WIDTH, SCREEN_HEIGHT);
    lv_obj_set_pos(form_cont, 0, 0);
    lv_obj_set_style_bg_opa(form_cont, 0, 0);
    lv_obj_set_style_border_width(form_cont, 0, 0);
    lv_obj_set_style_pad_all(form_cont, 0, 0);
    lv_obj_clear_flag(form_cont, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(form_cont, LV_SCROLLBAR_MODE_OFF);
    
    lv_obj_t * title = lv_label_create(form_cont);
    lv_label_set_text(title, "DADOS DA CERVEJA");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 15);
    
    create_form_row(form_cont, "Nome da Cerveja:", &ta_name, beerName.c_str(), 50);
    create_form_row(form_cont, "Estilo / Tipo:", &ta_type, beerType.c_str(), 100);
    
    char ibu_buf[16];
    sprintf(ibu_buf, "%d", beerIBU);
    create_form_row(form_cont, "Amargor (IBU):", &ta_ibu, ibu_buf, 150);
    
    char abv_buf[16];
    sprintf(abv_buf, "%.1f", beerABV);
    create_form_row(form_cont, "Alcool (ABV %):", &ta_abv, abv_buf, 200);
    
    lv_obj_t * btn_cancel = lv_btn_create(form_cont);
    lv_obj_set_size(btn_cancel, 120, 40);
    lv_obj_align(btn_cancel, LV_ALIGN_BOTTOM_LEFT, 20, -15);
    lv_obj_set_style_bg_color(btn_cancel, lv_color_hex(0x444444), 0);
    lv_obj_add_event_cb(btn_cancel, chopp_cancel_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_cancel = lv_label_create(btn_cancel);
    lv_label_set_text(lbl_cancel, "CANCELAR");
    lv_obj_center(lbl_cancel);
    
    lv_obj_t * btn_save = lv_btn_create(form_cont);
    lv_obj_set_size(btn_save, 120, 40);
    lv_obj_align(btn_save, LV_ALIGN_BOTTOM_RIGHT, -20, -15);
    lv_obj_set_style_bg_color(btn_save, lv_color_hex(0x10b981), 0);
    lv_obj_add_event_cb(btn_save, chopp_save_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t * lbl_save = lv_label_create(btn_save);
    lv_label_set_text(lbl_save, "SALVAR");
    lv_obj_center(lbl_save);
    
    kb_chopp = lv_keyboard_create(scr_chopp_edit);
    lv_obj_set_size(kb_chopp, SCREEN_WIDTH, 200);
    lv_obj_align(kb_chopp, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(kb_chopp, LV_OBJ_FLAG_HIDDEN);
}

void build_app_dashboard() {
  lv_label_set_text(dots, "");

  lv_obj_t * tv = lv_tileview_create(content_view);
  lv_obj_set_size(tv, 450, 240);
  lv_obj_remove_style(tv, NULL, LV_PART_SCROLLBAR); 
  lv_obj_set_style_bg_color(tv, lv_color_hex(0x0a0a0a), 0);
  lv_obj_set_style_bg_opa(tv, 255, 0);
  lv_obj_set_style_border_width(tv, 0, 0);

  lv_obj_t * t1 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_NONE);
  lv_obj_set_style_bg_opa(t1, 0, 0);
  lv_obj_set_style_border_width(t1, 0, 0);

  // Top Card (DADOS DA CERVEJA)
  lv_obj_t * card_left = lv_obj_create(t1);
  lv_obj_set_size(card_left, 450, 115);
  lv_obj_align(card_left, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_bg_color(card_left, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(card_left, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(card_left, 8, 0);
  lv_obj_clear_flag(card_left, LV_OBJ_FLAG_SCROLLABLE);

  // Make the entire card clickable
  lv_obj_add_flag(card_left, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card_left, chopp_edit_click_cb, LV_EVENT_CLICKED, NULL);

  lbl_chopp_name = lv_label_create(card_left);
  lv_label_set_text(lbl_chopp_name, beerName.c_str());
  lv_obj_set_style_text_font(lbl_chopp_name, &lv_font_montserrat_extrabold_48, 0);
  lv_obj_set_style_text_color(lbl_chopp_name, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_chopp_name, LV_ALIGN_CENTER, 0, -15);

  lv_obj_t * lbl_type = lv_label_create(card_left);
  lv_label_set_text(lbl_type, beerType.c_str());
  lv_obj_set_style_text_font(lbl_type, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_type, lv_color_hex(0xf59e0b), 0);
  lv_obj_align_to(lbl_type, lbl_chopp_name, LV_ALIGN_OUT_BOTTOM_MID, 0, 0);

  lv_obj_t * lbl_stats = lv_label_create(card_left);
  char stats[64];
  sprintf(stats, "IBU: %d   |   ABV: %.1f%%", beerIBU, beerABV);
  lv_label_set_text(lbl_stats, stats);
  lv_obj_set_style_text_font(lbl_stats, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(lbl_stats, lv_color_hex(0x888888), 0);
  lv_obj_align_to(lbl_stats, lbl_type, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);

  // Bottom Left Card (GELADEIRA TEMP)
  lv_obj_t * card_right = lv_obj_create(t1);
  lv_obj_set_size(card_right, 140, 115);
  lv_obj_align(card_right, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(card_right, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(card_right, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(card_right, 8, 0);
  lv_obj_clear_flag(card_right, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t * r_title = lv_label_create(card_right);
  lv_label_set_text(r_title, "TEMP. GELADEIRA");
  lv_obj_set_style_text_font(r_title, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(r_title, lv_color_hex(0x888888), 0);
  lv_obj_align(r_title, LV_ALIGN_TOP_LEFT, 0, 0);
  
  lbl_chopp_temp = lv_label_create(card_right);
  lv_label_set_text(lbl_chopp_temp, "--");
  lv_obj_set_style_text_font(lbl_chopp_temp, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lbl_chopp_temp, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_chopp_temp, LV_ALIGN_BOTTOM_LEFT, 0, 5);
  
  lbl_chopp_temp_unit = lv_label_create(card_right);
  lv_label_set_text(lbl_chopp_temp_unit, "C");
  lv_obj_set_style_text_font(lbl_chopp_temp_unit, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_chopp_temp_unit, lv_color_hex(0x888888), 0);
  lv_obj_align_to(lbl_chopp_temp_unit, lbl_chopp_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);

  // Bottom Mid Card (SETPOINT)
  lv_obj_t * card_sp = lv_obj_create(t1);
  lv_obj_set_size(card_sp, 140, 115);
  lv_obj_align(card_sp, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_color(card_sp, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(card_sp, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(card_sp, 8, 0);
  lv_obj_clear_flag(card_sp, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_add_flag(card_sp, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(card_sp, setpoint_click_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * sp_title = lv_label_create(card_sp);
  lv_label_set_text(sp_title, "SETPOINT");
  lv_obj_set_style_text_font(sp_title, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(sp_title, lv_color_hex(0x888888), 0);
  lv_obj_align(sp_title, LV_ALIGN_TOP_LEFT, 0, 0);
  
  lbl_chopp_setpoint = lv_label_create(card_sp);
  lv_label_set_text(lbl_chopp_setpoint, "--");
  lv_obj_set_style_text_font(lbl_chopp_setpoint, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lbl_chopp_setpoint, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_chopp_setpoint, LV_ALIGN_BOTTOM_LEFT, 0, 5);
  
  lbl_chopp_setpoint_unit = lv_label_create(card_sp);
  lv_label_set_text(lbl_chopp_setpoint_unit, "C");
  lv_obj_set_style_text_font(lbl_chopp_setpoint_unit, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_chopp_setpoint_unit, lv_color_hex(0x888888), 0);
  lv_obj_align_to(lbl_chopp_setpoint_unit, lbl_chopp_setpoint, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);

  // Bottom Right Card (UMIDADE)
  lv_obj_t * card_hum = lv_obj_create(t1);
  lv_obj_set_size(card_hum, 140, 115);
  lv_obj_align(card_hum, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(card_hum, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(card_hum, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(card_hum, 8, 0);
  lv_obj_clear_flag(card_hum, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t * h_title = lv_label_create(card_hum);
  lv_label_set_text(h_title, "UMIDADE INTERNA");
  lv_obj_set_style_text_font(h_title, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(h_title, lv_color_hex(0x888888), 0);
  lv_obj_align(h_title, LV_ALIGN_TOP_LEFT, 0, 0);
  
  lbl_chopp_hum = lv_label_create(card_hum);
  lv_label_set_text(lbl_chopp_hum, "--");
  lv_obj_set_style_text_font(lbl_chopp_hum, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lbl_chopp_hum, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_chopp_hum, LV_ALIGN_BOTTOM_LEFT, 0, 5);
}

// ----------------------------------------------------
void build_telemetry_page1(lv_obj_t * parent_tile) {
  // ==================== PAGINA 1 ====================
  lv_obj_t * t1 = parent_tile;
  lv_obj_set_style_bg_opa(t1, 0, 0);
  lv_obj_set_style_border_width(t1, 0, 0);
  
  // Card 1: Temp Mosto (218x115)
  lv_obj_t * c1 = lv_obj_create(t1);
  lv_obj_set_size(c1, 218, 115);
  lv_obj_set_pos(c1, 0, 0);
  lv_obj_set_style_bg_color(c1, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(c1, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(c1, 8, 0);
  lv_obj_clear_flag(c1, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t * lbl1 = lv_label_create(c1);
  lv_label_set_text(lbl1, "TEMP MOSTO\nS1 - Poco Termico");
  lv_obj_set_style_text_font(lbl1, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(lbl1, lv_color_hex(0x888888), 0);
  lv_obj_align(lbl1, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_telemetry_temp = lv_label_create(c1);
  lv_label_set_text(lbl_telemetry_temp, "--");
  lv_obj_set_style_text_font(lbl_telemetry_temp, &lv_font_montserrat_extrabold_48, 0); 
  lv_obj_set_style_text_color(lbl_telemetry_temp, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_telemetry_temp, LV_ALIGN_BOTTOM_LEFT, 0, 5);

  lbl_telemetry_temp_unit = lv_label_create(c1);
  lv_label_set_text(lbl_telemetry_temp_unit, "C");
  lv_obj_set_style_text_font(lbl_telemetry_temp_unit, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_telemetry_temp_unit, lv_color_hex(0x888888), 0);
  lv_obj_align_to(lbl_telemetry_temp_unit, lbl_telemetry_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);

  // Card 2: Gravidade (218x115)
  lv_obj_t * c2 = lv_obj_create(t1);
  lv_obj_set_size(c2, 218, 115);
  lv_obj_set_pos(c2, 232, 0); // 218 + 14 gap
  lv_obj_set_style_bg_color(c2, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(c2, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(c2, 8, 0);
  lv_obj_clear_flag(c2, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t * lbl2 = lv_label_create(c2);
  lv_label_set_text(lbl2, "GRAVIDADE (SG)\niSpindel");
  lv_obj_set_style_text_font(lbl2, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(lbl2, lv_color_hex(0x888888), 0);
  lv_obj_align(lbl2, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_telemetry_sg = lv_label_create(c2);
  lv_label_set_text(lbl_telemetry_sg, "--");
  lv_obj_set_style_text_font(lbl_telemetry_sg, &lv_font_montserrat_extrabold_48, 0);
  lv_obj_set_style_text_color(lbl_telemetry_sg, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_telemetry_sg, LV_ALIGN_BOTTOM_LEFT, 0, 5);

  // 4 Bottom Cards: 103x120
  const char * names[4] = {"Sensor 2", "Umidade", "SetPoint", "Final Gravity"};
  const char * vals[4] = {"--", "--", "--", "--"};
  lv_obj_t * labels_ptr[4];
  
  for(int i=0; i<4; i++) {
    lv_obj_t * m = lv_obj_create(t1);
    lv_obj_set_size(m, 103, 120);
    lv_obj_set_pos(m, i * (103+12), 134);
    lv_obj_set_style_bg_color(m, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(m, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(m, 8, 0);
    lv_obj_clear_flag(m, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(m, 12, 0);
    
    if (i == 2) {
        lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(m, setpoint_click_cb, LV_EVENT_CLICKED, NULL);
    } else if (i == 3) {
        lv_obj_add_flag(m, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(m, fg_click_cb, LV_EVENT_CLICKED, NULL);
    }
    
    lv_obj_t * mt = lv_label_create(m);
    lv_label_set_text(mt, names[i]);
    lv_obj_set_style_text_color(mt, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(mt, &lv_font_montserrat_10, 0);
    lv_obj_align(mt, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t * mv = lv_label_create(m);
    lv_label_set_text(mv, vals[i]);
    lv_obj_set_style_text_color(mv, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(mv, &lv_font_montserrat_24, 0);
    lv_obj_align(mv, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    
    if (i == 0) { // Sensor 2
        lbl_telemetry_t2_unit = lv_label_create(m);
        lv_label_set_text(lbl_telemetry_t2_unit, "C");
        lv_obj_set_style_text_font(lbl_telemetry_t2_unit, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl_telemetry_t2_unit, lv_color_hex(0x888888), 0);
        lv_obj_align_to(lbl_telemetry_t2_unit, mv, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -3);
    } else if (i == 2) { // SetPoint
        lbl_telemetry_setpoint_unit = lv_label_create(m);
        lv_label_set_text(lbl_telemetry_setpoint_unit, "C");
        lv_obj_set_style_text_font(lbl_telemetry_setpoint_unit, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl_telemetry_setpoint_unit, lv_color_hex(0x888888), 0);
        lv_obj_align_to(lbl_telemetry_setpoint_unit, mv, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -3);
    }
    
    labels_ptr[i] = mv;
  }
  
  lbl_telemetry_t2 = labels_ptr[0];
  lbl_telemetry_hum = labels_ptr[1];
  lbl_telemetry_setpoint = labels_ptr[2];
  lbl_telemetry_fg = labels_ptr[3];


}

void build_telemetry_relays(lv_obj_t * parent_tile) {
  // ==================== PAGINA 2 ====================
  lv_obj_t * t2 = parent_tile;
  lv_obj_set_style_bg_opa(t2, 0, 0);
  lv_obj_set_style_border_width(t2, 0, 0);
  
  // Card 1: Rele Aquecimento (218x115)
  lv_obj_t * c3 = lv_obj_create(t2);
  lv_obj_set_size(c3, 218, 115);
  lv_obj_set_pos(c3, 0, 0);
  lv_obj_set_style_bg_color(c3, lv_color_hex(0x1a0f12), 0);
  lv_obj_set_style_border_color(c3, lv_color_hex(0x4c1d28), 0);
  lv_obj_set_style_radius(c3, 8, 0);
  lv_obj_clear_flag(c3, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t * lbl3 = lv_label_create(c3);
  lv_label_set_text(lbl3, "RELE AQUECIMENTO\n#888888 Resistencia#");
  lv_label_set_recolor(lbl3, true);
  lv_obj_set_style_text_font(lbl3, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(lbl3, lv_color_hex(0xf43f5e), 0);
  lv_obj_align(lbl3, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_telemetry_heater = lv_label_create(c3);
  lv_label_set_text(lbl_telemetry_heater, "--");
  lv_obj_set_style_text_font(lbl_telemetry_heater, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_telemetry_heater, lv_color_hex(0xf43f5e), 0);
  lv_obj_align(lbl_telemetry_heater, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // Card 2: Rele Frio (218x120)
  lv_obj_t * c4 = lv_obj_create(t2);
  lv_obj_set_size(c4, 218, 120);
  lv_obj_set_pos(c4, 232, 0);
  lv_obj_set_style_bg_color(c4, lv_color_hex(0x0f172a), 0);
  lv_obj_set_style_border_color(c4, lv_color_hex(0x1e3a8a), 0);
  lv_obj_set_style_radius(c4, 8, 0);
  lv_obj_clear_flag(c4, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t * lbl4 = lv_label_create(c4);
  lv_label_set_text(lbl4, "RELE FRIO\n#888888 Compressor#");
  lv_label_set_recolor(lbl4, true);
  lv_obj_set_style_text_font(lbl4, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(lbl4, lv_color_hex(0x3b82f6), 0);
  lv_obj_align(lbl4, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_telemetry_cooler = lv_label_create(c4);
  lv_label_set_text(lbl_telemetry_cooler, "--");
  lv_obj_set_style_text_font(lbl_telemetry_cooler, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl_telemetry_cooler, lv_color_hex(0x3b82f6), 0);
  lv_obj_align(lbl_telemetry_cooler, LV_ALIGN_BOTTOM_LEFT, 0, 0);

  // 3 Bottom Cards: 140x110
  const char * names2[3] = {"Sinal PWM", "Uptime", "Memoria Livre"};
  const char * vals2[3] = {"0%", "--", "--"};
  lv_obj_t * ptrs2[3];
  
  for(int i=0; i<3; i++) {
    lv_obj_t * m = lv_obj_create(t2);
    lv_obj_set_size(m, 140, 110);
    lv_obj_set_pos(m, i * (140+15), 125); 
    lv_obj_set_style_bg_color(m, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(m, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(m, 8, 0);
    lv_obj_clear_flag(m, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_pad_all(m, 12, 0);
    
    lv_obj_t * mt = lv_label_create(m);
    lv_label_set_text(mt, names2[i]);
    lv_obj_set_style_text_color(mt, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(mt, &lv_font_montserrat_10, 0);
    lv_obj_align(mt, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t * mv = lv_label_create(m);
    lv_label_set_text(mv, vals2[i]);
    lv_obj_set_style_text_color(mv, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(mv, &lv_font_montserrat_24, 0);
    lv_obj_align(mv, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    
    ptrs2[i] = mv;
  }
  
  lbl_telemetry_pwm = ptrs2[0];
  lbl_telemetry_uptime = ptrs2[1];
  lbl_telemetry_ram = ptrs2[2];

}

void build_telemetry_chart(lv_obj_t * parent_tile) {
  // ==================== PAGINA 3: GRAFICO ====================
  lv_obj_t * t3 = parent_tile;
  lv_obj_set_style_bg_opa(t3, 0, 0);
  lv_obj_set_style_border_width(t3, 0, 0);

  lv_obj_t * c_chart = lv_obj_create(t3);
  lv_obj_set_size(c_chart, 450, 240);
  lv_obj_set_pos(c_chart, 0, 0);
  lv_obj_set_style_bg_color(c_chart, lv_color_hex(0x0f0f0f), 0);
  lv_obj_set_style_border_color(c_chart, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(c_chart, 8, 0);
  lv_obj_clear_flag(c_chart, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t * lbl_c = lv_label_create(c_chart);
  lv_label_set_text(lbl_c, "GRAVIDADE ISPINDEL (HISTORICO)");
  lv_obj_set_style_text_font(lbl_c, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(lbl_c, lv_color_hex(0x888888), 0);
  lv_obj_align(lbl_c, LV_ALIGN_TOP_LEFT, 0, 0);

  ispindel_chart = lv_chart_create(c_chart);
  lv_obj_set_size(ispindel_chart, 400, 200);
  lv_obj_align(ispindel_chart, LV_ALIGN_CENTER, 0, 10);
  lv_chart_set_type(ispindel_chart, LV_CHART_TYPE_LINE);
  lv_obj_set_style_bg_opa(ispindel_chart, 0, 0);
  lv_obj_set_style_border_width(ispindel_chart, 0, 0);
  lv_chart_set_div_line_count(ispindel_chart, 5, 5);
  
  // Set Y-axis range from 1.000 to 1.100 (multiplied by 1000 = 1000 to 1100)
  lv_chart_set_range(ispindel_chart, LV_CHART_AXIS_PRIMARY_Y, 1000, 1100);
  lv_chart_set_point_count(ispindel_chart, ISPINDEL_HISTORY_SIZE);

  ser_ispindel = lv_chart_add_series(ispindel_chart, lv_color_hex(0xf59e0b), LV_CHART_AXIS_PRIMARY_Y);
  
  // Plot initial data
  ui_update_ispindel_chart();
}

void build_app_telemetry() {
  lv_label_set_text(dots, LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET); 

  lv_obj_t * tv = lv_tileview_create(content_view);
  lv_obj_set_size(tv, 450, 240);
  lv_obj_remove_style(tv, NULL, LV_PART_SCROLLBAR); 
  lv_obj_set_style_bg_color(tv, lv_color_hex(0x0a0a0a), 0);
  lv_obj_set_style_bg_opa(tv, 255, 0);
  lv_obj_set_style_anim_time(tv, 150, 0);
  lv_obj_set_style_border_width(tv, 0, 0);

  lv_obj_t * t0 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
  lv_obj_set_style_bg_opa(t0, 0, 0);
  build_telemetry_page1(t0);

  lv_obj_t * t1 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT);
  lv_obj_set_style_bg_opa(t1, 0, 0);
  build_telemetry_chart(t1);
}


static lv_obj_t * wizard_tv;
static lv_obj_t * roller_style;
static lv_obj_t * roller_vol;
static lv_obj_t * lbl_wiz_og;
static lv_obj_t * lbl_wiz_fg;
static lv_obj_t * lbl_wiz_sum_style;
static lv_obj_t * lbl_wiz_sum_vol;
static lv_obj_t * lbl_wiz_sum_og;
static lv_obj_t * lbl_wiz_sum_fg;

static float wiz_og = 1.050;
static float wiz_fg = 1.010;

static void btn_wiz_nav_cb(lv_event_t * e) {
    int target_tile = (int)(uintptr_t)lv_event_get_user_data(e);
    
    // Se estiver indo para o resumo (tile 6), atualiza os labels
    if(target_tile == 6) {
        char buf[32];
        lv_roller_get_selected_str(roller_style, buf, sizeof(buf));
        lv_label_set_text(lbl_wiz_sum_style, buf);
        
        lv_roller_get_selected_str(roller_vol, buf, sizeof(buf));
        lv_label_set_text(lbl_wiz_sum_vol, buf);
        
        sprintf(buf, "%.3f", wiz_og);
        lv_label_set_text(lbl_wiz_sum_og, buf);
        
        sprintf(buf, "%.3f", wiz_fg);
        lv_label_set_text(lbl_wiz_sum_fg, buf);
    }
    
    lv_obj_set_tile_id(wizard_tv, target_tile, 0, LV_ANIM_ON);
}

static void btn_wiz_og_cb(lv_event_t * e) {
    int delta = (int)(uintptr_t)lv_event_get_user_data(e);
    wiz_og += (delta * 0.001);
    char buf[16];
    sprintf(buf, "%.3f", wiz_og);
    lv_label_set_text(lbl_wiz_og, buf);
}

static void btn_wiz_fg_cb(lv_event_t * e) {
    int delta = (int)(uintptr_t)lv_event_get_user_data(e);
    wiz_fg += (delta * 0.001);
    char buf[16];
    sprintf(buf, "%.3f", wiz_fg);
    lv_label_set_text(lbl_wiz_fg, buf);
}

static void btn_wizard_start_cb(lv_event_t * e) {
    char buf[32];
    lv_roller_get_selected_str(roller_style, buf, sizeof(buf));
    ferm_start_preset(buf); // perfil pelo estilo + aviso ao servidor (fermentation.cpp)
    open_app(2); // Vai para Fermentacao ativa
}

// Helper para criar botoes de navegacao
void create_nav_buttons(lv_obj_t * parent, int prev, int next) {
    if (prev >= 0) {
        lv_obj_t * btn_prev = lv_btn_create(parent);
        lv_obj_set_size(btn_prev, 100, 36);
        lv_obj_align(btn_prev, LV_ALIGN_BOTTOM_LEFT, 10, -10);
        lv_obj_set_style_bg_color(btn_prev, lv_color_hex(0x111111), 0);
        lv_obj_set_style_border_color(btn_prev, lv_color_hex(0x333333), 0);
        lv_obj_set_style_border_width(btn_prev, 1, 0);
        lv_obj_add_event_cb(btn_prev, btn_wiz_nav_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)prev);
        lv_obj_t * l_prev = lv_label_create(btn_prev);
        lv_label_set_text(l_prev, "< VOLTAR");
        lv_obj_set_style_text_color(l_prev, lv_color_hex(0x888888), 0);
        lv_obj_center(l_prev);
    }
    
    if (next >= 0) {
        lv_obj_t * btn_next = lv_btn_create(parent);
        lv_obj_set_size(btn_next, 100, 36);
        lv_obj_align(btn_next, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
        lv_obj_set_style_bg_color(btn_next, lv_color_hex(0xFFFFFF), 0);
        lv_obj_add_event_cb(btn_next, btn_wiz_nav_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)next);
        lv_obj_t * l_next = lv_label_create(btn_next);
        lv_label_set_text(l_next, "AVANCAR >");
        lv_obj_set_style_text_color(l_next, lv_color_hex(0x000000), 0);
        lv_obj_center(l_next);
    }
}

void build_app_wizard() {
  wizard_tv = lv_tileview_create(content_view);
  lv_obj_set_size(wizard_tv, 450, 240);
  lv_obj_remove_style(wizard_tv, NULL, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(wizard_tv, lv_color_hex(0x050505), 0);
  
  // TILE 0: Intro
  lv_obj_t * t0 = lv_tileview_add_tile(wizard_tv, 0, 0, LV_DIR_RIGHT);
  lv_obj_t * lbl = lv_label_create(t0);
  lv_label_set_text(lbl, "Nova Fermentacao");
  lv_obj_set_style_text_font(lbl, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl, LV_ALIGN_CENTER, 0, -30);

  lv_obj_t * btn = lv_btn_create(t0);
  lv_obj_set_size(btn, 140, 40);
  lv_obj_align(btn, LV_ALIGN_CENTER, 0, 30);
  lv_obj_set_style_bg_color(btn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_add_event_cb(btn, btn_wiz_nav_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)1);
  lv_obj_t * lbl_btn = lv_label_create(btn);
  lv_label_set_text(lbl_btn, "COMECAR >");
  lv_obj_set_style_text_color(lbl_btn, lv_color_hex(0x000000), 0);
  lv_obj_center(lbl_btn);
  
  // TILE 1: Style Selection
  lv_obj_t * t1 = lv_tileview_add_tile(wizard_tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t * lbl_t1 = lv_label_create(t1);
  lv_label_set_text(lbl_t1, "Escolha o Estilo");
  lv_obj_set_style_text_font(lbl_t1, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_t1, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_t1, LV_ALIGN_TOP_MID, 0, 10);
  
  roller_style = lv_roller_create(t1);
  lv_roller_set_options(roller_style, "Ale\nLager\nWeiss\nStout", LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller_style, 3);
  lv_obj_set_width(roller_style, 250);
  lv_obj_align(roller_style, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(roller_style, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_color(roller_style, lv_color_hex(0x333333), LV_PART_SELECTED);
  create_nav_buttons(t1, 0, 2);

  // TILE 2: Volume Selection
  lv_obj_t * t2 = lv_tileview_add_tile(wizard_tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t * lbl_t2 = lv_label_create(t2);
  lv_label_set_text(lbl_t2, "Volume do Lote");
  lv_obj_set_style_text_font(lbl_t2, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_t2, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_t2, LV_ALIGN_TOP_MID, 0, 10);
  
  roller_vol = lv_roller_create(t2);
  lv_roller_set_options(roller_vol, "20 L\n30 L\n50 L", LV_ROLLER_MODE_NORMAL);
  lv_roller_set_visible_row_count(roller_vol, 3);
  lv_obj_set_width(roller_vol, 250);
  lv_obj_align(roller_vol, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_bg_color(roller_vol, lv_color_hex(0x111111), 0);
  lv_obj_set_style_bg_color(roller_vol, lv_color_hex(0x333333), LV_PART_SELECTED);
  create_nav_buttons(t2, 1, 3);

  // TILE 3: OG Selection
  lv_obj_t * t3 = lv_tileview_add_tile(wizard_tv, 3, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t * lbl_t3 = lv_label_create(t3);
  lv_label_set_text(lbl_t3, "Densidade Inicial (OG)");
  lv_obj_set_style_text_color(lbl_t3, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_t3, LV_ALIGN_TOP_MID, 0, 10);
  
  lbl_wiz_og = lv_label_create(t3);
  lv_label_set_text(lbl_wiz_og, "1.050");
  lv_obj_set_style_text_font(lbl_wiz_og, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lbl_wiz_og, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_wiz_og, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t * btn_og_minus = lv_btn_create(t3);
  lv_obj_set_size(btn_og_minus, 50, 50);
  lv_obj_align_to(btn_og_minus, lbl_wiz_og, LV_ALIGN_OUT_LEFT_MID, -20, 0);
  lv_obj_set_style_bg_color(btn_og_minus, lv_color_hex(0x222222), 0);
  lv_obj_add_event_cb(btn_og_minus, btn_wiz_og_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)(-1));
  lv_obj_t * l_om = lv_label_create(btn_og_minus);
  lv_label_set_text(l_om, "-"); lv_obj_center(l_om);
  
  lv_obj_t * btn_og_plus = lv_btn_create(t3);
  lv_obj_set_size(btn_og_plus, 50, 50);
  lv_obj_align_to(btn_og_plus, lbl_wiz_og, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
  lv_obj_set_style_bg_color(btn_og_plus, lv_color_hex(0x222222), 0);
  lv_obj_add_event_cb(btn_og_plus, btn_wiz_og_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)(1));
  lv_obj_t * l_op = lv_label_create(btn_og_plus);
  lv_label_set_text(l_op, "+"); lv_obj_center(l_op);

  create_nav_buttons(t3, 2, 4);

  // TILE 4: FG Selection
  lv_obj_t * t4 = lv_tileview_add_tile(wizard_tv, 4, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t * lbl_t4 = lv_label_create(t4);
  lv_label_set_text(lbl_t4, "Densidade Final (FG)");
  lv_obj_set_style_text_color(lbl_t4, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_t4, LV_ALIGN_TOP_MID, 0, 10);
  
  lbl_wiz_fg = lv_label_create(t4);
  lv_label_set_text(lbl_wiz_fg, "1.010");
  lv_obj_set_style_text_font(lbl_wiz_fg, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lbl_wiz_fg, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_wiz_fg, LV_ALIGN_CENTER, 0, -10);

  lv_obj_t * btn_fg_minus = lv_btn_create(t4);
  lv_obj_set_size(btn_fg_minus, 50, 50);
  lv_obj_align_to(btn_fg_minus, lbl_wiz_fg, LV_ALIGN_OUT_LEFT_MID, -20, 0);
  lv_obj_set_style_bg_color(btn_fg_minus, lv_color_hex(0x222222), 0);
  lv_obj_add_event_cb(btn_fg_minus, btn_wiz_fg_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)(-1));
  lv_obj_t * l_fm = lv_label_create(btn_fg_minus);
  lv_label_set_text(l_fm, "-"); lv_obj_center(l_fm);
  
  lv_obj_t * btn_fg_plus = lv_btn_create(t4);
  lv_obj_set_size(btn_fg_plus, 50, 50);
  lv_obj_align_to(btn_fg_plus, lbl_wiz_fg, LV_ALIGN_OUT_RIGHT_MID, 20, 0);
  lv_obj_set_style_bg_color(btn_fg_plus, lv_color_hex(0x222222), 0);
  lv_obj_add_event_cb(btn_fg_plus, btn_wiz_fg_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)(1));
  lv_obj_t * l_fp = lv_label_create(btn_fg_plus);
  lv_label_set_text(l_fp, "+"); lv_obj_center(l_fp);

  create_nav_buttons(t4, 3, 5);

  // TILE 5: Ramps preview
  lv_obj_t * t5 = lv_tileview_add_tile(wizard_tv, 5, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_t * lbl_t5 = lv_label_create(t5);
  lv_label_set_text(lbl_t5, "Perfil de Rampas");
  lv_obj_set_style_text_color(lbl_t5, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_t5, LV_ALIGN_TOP_MID, 0, 10);
  
  lv_obj_t * ramps_list = lv_obj_create(t5);
  lv_obj_set_size(ramps_list, 300, 120);
  lv_obj_align(ramps_list, LV_ALIGN_CENTER, 0, -10);
  lv_obj_set_style_bg_color(ramps_list, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(ramps_list, lv_color_hex(0x222222), 0);
  
  lv_obj_t * lr1 = lv_label_create(ramps_list);
  lv_label_set_text(lr1, "1. Primaria: 18.0C / 5d");
  lv_obj_set_style_text_color(lr1, lv_color_hex(0xffffff), 0);
  lv_obj_t * lr2 = lv_label_create(ramps_list);
  lv_label_set_text(lr2, "2. Diacetil: 22.0C / 2d");
  lv_obj_set_style_text_color(lr2, lv_color_hex(0xffffff), 0);
  lv_obj_align(lr2, LV_ALIGN_TOP_LEFT, 0, 30);
  lv_obj_t * lr3 = lv_label_create(ramps_list);
  lv_label_set_text(lr3, "3. Cold Crash: 2.0C / 7d");
  lv_obj_set_style_text_color(lr3, lv_color_hex(0xffffff), 0);
  lv_obj_align(lr3, LV_ALIGN_TOP_LEFT, 0, 60);

  create_nav_buttons(t5, 4, 6);

  // TILE 6: Summary
  lv_obj_t * t6 = lv_tileview_add_tile(wizard_tv, 6, 0, LV_DIR_LEFT);
  lv_obj_t * grid = lv_obj_create(t6);
  lv_obj_set_size(grid, 340, 160);
  lv_obj_align(grid, LV_ALIGN_TOP_MID, 0, 10);
  lv_obj_set_style_bg_opa(grid, 0, 0);
  lv_obj_set_style_border_width(grid, 0, 0);
  
  // Style Box
  lv_obj_t * b_style = lv_obj_create(grid);
  lv_obj_set_size(b_style, 160, 60);
  lv_obj_set_pos(b_style, 0, 0);
  lv_obj_set_style_bg_color(b_style, lv_color_hex(0x111111), 0);
  lv_obj_t * l1 = lv_label_create(b_style); lv_label_set_text(l1, "Estilo");
  lv_obj_set_style_text_color(l1, lv_color_hex(0x888888), 0);
  lbl_wiz_sum_style = lv_label_create(b_style); lv_label_set_text(lbl_wiz_sum_style, "--");
  lv_obj_set_style_text_color(lbl_wiz_sum_style, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_wiz_sum_style, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  
  // Vol Box
  lv_obj_t * b_vol = lv_obj_create(grid);
  lv_obj_set_size(b_vol, 160, 60);
  lv_obj_set_pos(b_vol, 170, 0);
  lv_obj_set_style_bg_color(b_vol, lv_color_hex(0x111111), 0);
  lv_obj_t * l2 = lv_label_create(b_vol); lv_label_set_text(l2, "Volume");
  lv_obj_set_style_text_color(l2, lv_color_hex(0x888888), 0);
  lbl_wiz_sum_vol = lv_label_create(b_vol); lv_label_set_text(lbl_wiz_sum_vol, "--");
  lv_obj_set_style_text_color(lbl_wiz_sum_vol, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_wiz_sum_vol, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  
  // OG Box
  lv_obj_t * b_og = lv_obj_create(grid);
  lv_obj_set_size(b_og, 160, 60);
  lv_obj_set_pos(b_og, 0, 70);
  lv_obj_set_style_bg_color(b_og, lv_color_hex(0x111111), 0);
  lv_obj_t * l3 = lv_label_create(b_og); lv_label_set_text(l3, "OG");
  lv_obj_set_style_text_color(l3, lv_color_hex(0x888888), 0);
  lbl_wiz_sum_og = lv_label_create(b_og); lv_label_set_text(lbl_wiz_sum_og, "--");
  lv_obj_set_style_text_color(lbl_wiz_sum_og, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_wiz_sum_og, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  
  // FG Box
  lv_obj_t * b_fg = lv_obj_create(grid);
  lv_obj_set_size(b_fg, 160, 60);
  lv_obj_set_pos(b_fg, 170, 70);
  lv_obj_set_style_bg_color(b_fg, lv_color_hex(0x111111), 0);
  lv_obj_t * l4 = lv_label_create(b_fg); lv_label_set_text(l4, "FG");
  lv_obj_set_style_text_color(l4, lv_color_hex(0x888888), 0);
  lbl_wiz_sum_fg = lv_label_create(b_fg); lv_label_set_text(lbl_wiz_sum_fg, "--");
  lv_obj_set_style_text_color(lbl_wiz_sum_fg, lv_color_hex(0xffffff), 0);
  lv_obj_align(lbl_wiz_sum_fg, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  
  lv_obj_t * btn_prev = lv_btn_create(t6);
  lv_obj_set_size(btn_prev, 100, 36);
  lv_obj_align(btn_prev, LV_ALIGN_BOTTOM_LEFT, 10, -10);
  lv_obj_set_style_bg_color(btn_prev, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(btn_prev, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(btn_prev, 1, 0);
  lv_obj_add_event_cb(btn_prev, btn_wiz_nav_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)5);
  lv_obj_t * l_prev = lv_label_create(btn_prev);
  lv_label_set_text(l_prev, "< VOLTAR");
  lv_obj_set_style_text_color(l_prev, lv_color_hex(0x888888), 0);
  lv_obj_center(l_prev);
  
  lv_obj_t * btn_start = lv_btn_create(t6);
  lv_obj_set_size(btn_start, 120, 36);
  lv_obj_align(btn_start, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
  lv_obj_set_style_bg_color(btn_start, lv_color_hex(0xFFFFFF), 0);
  lv_obj_add_event_cb(btn_start, btn_wizard_start_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t * l_start = lv_label_create(btn_start);
  lv_label_set_text(l_start, "INICIAR >");
  lv_obj_set_style_text_color(l_start, lv_color_hex(0x000000), 0);
  lv_obj_center(l_start);
}



static void btn_ferm_control_cb(lv_event_t * e) {
    int action = (int)(uintptr_t)lv_event_get_user_data(e);
    if (action == 3) { // FINALIZAR
        ferm_stop_batch(); // encerra o perfil e avisa o servidor (fermentation.cpp)

        open_app(2); // Reloads Fermentation, which will now show Wizard
    }
}

void build_fermentation_ramps(lv_obj_t * parent_tile) {
  // ==================== PAGINA 2: Etapas ====================
  lv_obj_t * t2 = parent_tile;
  lv_obj_set_style_bg_opa(t2, 0, 0);
  lv_obj_set_style_border_width(t2, 0, 0);

  // Linha vertical do stepper
  lv_obj_t * line = lv_obj_create(t2);
  lv_obj_set_size(line, 2, 200);
  lv_obj_set_pos(line, 28, 25);
  lv_obj_set_style_bg_color(line, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(line, 0, 0);
  lv_obj_clear_flag(line, LV_OBJ_FLAG_SCROLLABLE);

  // Etapas
  // Le as rampas globais (dinamicas)
  for(int i=0; i<numRampSteps; i++) {
    bool is_active = (isRampActive && currentRampStep == i);
    
    lv_obj_t * sc = lv_obj_create(t2);
    lv_obj_set_size(sc, 290, 75);
    lv_obj_set_pos(sc, 0, i * (75+10));
    lv_obj_set_style_bg_color(sc, lv_color_hex(is_active ? 0x1A1A1A : 0x111111), 0);
    lv_obj_set_style_border_color(sc, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(sc, 12, 0);
    lv_obj_set_style_pad_all(sc, 0, 0);
    lv_obj_clear_flag(sc, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t * circle = lv_obj_create(sc);
    lv_obj_set_size(circle, 32, 32);
    lv_obj_align(circle, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(is_active ? 0xFFFFFF : 0x111111), 0);
    lv_obj_set_style_border_color(circle, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(circle, is_active ? 0 : 2, 0);
    lv_obj_set_style_radius(circle, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t * c_txt = lv_label_create(circle);
    char buf_num[4];
    if (is_active) sprintf(buf_num, "%s", LV_SYMBOL_PLAY);
    else sprintf(buf_num, "%d", i+1);
    lv_label_set_text(c_txt, buf_num);
    lv_obj_set_style_text_color(c_txt, lv_color_hex(is_active ? 0x000000 : 0x888888), 0);
    lv_obj_center(c_txt);
    
    lv_obj_t * t = lv_label_create(sc);
    lv_label_set_text(t, rampSteps[i].name);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(is_active ? 0xFFFFFF : 0x888888), 0);
    lv_obj_align(t, LV_ALIGN_TOP_LEFT, 60, 15);
    
    lv_obj_t * s = lv_label_create(sc);
    char buf_sub[64];
    // Duracao: dias inteiros como "7d"; fracionados com uma casa ("1.5d")
    float dd = rampSteps[i].durationDays;
    if (fabsf(dd - roundf(dd)) < 0.05f) sprintf(buf_sub, "%s %.1f C   %s %dd", LV_SYMBOL_TINT, rampSteps[i].targetTemp, LV_SYMBOL_BELL, (int)roundf(dd));
    else sprintf(buf_sub, "%s %.1f C   %s %.1fd", LV_SYMBOL_TINT, rampSteps[i].targetTemp, LV_SYMBOL_BELL, dd);
    lv_label_set_text(s, buf_sub);
    lv_obj_set_style_text_font(s, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(s, lv_color_hex(0x666666), 0);
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, 60, -15);

    if(is_active) {
      lv_obj_t * at = lv_label_create(sc);
      lv_label_set_text(at, "ATIVO");
      lv_obj_set_style_text_font(at, &lv_font_montserrat_10, 0);
      lv_obj_set_style_text_color(at, lv_color_hex(0xFFFFFF), 0);
      lv_obj_align(at, LV_ALIGN_TOP_RIGHT, -10, 15);
      
      lv_obj_t * sec = lv_label_create(sc);
      lv_label_set_text(sec, "0s");
      lv_obj_set_style_text_font(sec, &lv_font_montserrat_12, 0);
      lv_obj_set_style_text_color(sec, lv_color_hex(0x888888), 0);
      lv_obj_align(sec, LV_ALIGN_BOTTOM_RIGHT, -10, -15);
    }
  }

  // Right Buttons
  const char * b_txt[4] = {LV_SYMBOL_PAUSE "\nPAUSAR", LV_SYMBOL_PREV "\nANTERIOR", LV_SYMBOL_NEXT "\nPROXIMA", LV_SYMBOL_STOP "\nFINALIZAR"};
  for(int i=0; i<4; i++) {
    lv_obj_t * b = lv_btn_create(t2);
    lv_obj_set_size(b, 140, 52);
    lv_obj_set_pos(b, 310, i*(52+10));
    lv_obj_set_style_bg_color(b, lv_color_hex(i==3 ? 0x2E1015 : 0xFFFFFF), 0);
    lv_obj_set_style_radius(b, 8, 0);
    lv_obj_add_event_cb(b, btn_ferm_control_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)i);
    
    lv_obj_t * l = lv_label_create(b);
    lv_label_set_text(l, b_txt[i]);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(i==3 ? 0xf43f5e : 0x000000), 0);
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(l, 2, 0);
    lv_obj_center(l);
  }

}

void build_fermentation_logs(lv_obj_t * parent_tile) {
  // ==================== PAGINA 3: Logs ====================
  lv_obj_t * t3 = parent_tile;
  lv_obj_set_style_bg_opa(t3, 0, 0);
  lv_obj_set_style_border_width(t3, 0, 0);

  lv_obj_t * add_btn = lv_btn_create(t3);
  lv_obj_set_size(add_btn, 130, 36);
  lv_obj_align(add_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(add_btn, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(add_btn, 8, 0);
  lv_obj_t * add_l = lv_label_create(add_btn);
  lv_label_set_text(add_l, "+ ADICIONAR");
  lv_obj_set_style_text_font(add_l, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(add_l, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_letter_space(add_l, 1, 0);
  lv_obj_center(add_l);

  // Linha do log
  lv_obj_t * log_line = lv_obj_create(t3);
  lv_obj_set_size(log_line, 2, 210);
  lv_obj_set_pos(log_line, 25, 45);
  lv_obj_set_style_bg_color(log_line, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(log_line, 0, 0);
  lv_obj_clear_flag(log_line, LV_OBJ_FLAG_SCROLLABLE);

  const char * badges[3] = {"SISTEMA", "SYSTEM_ACTION", "SYSTEM_ACTION"};
  const char * dates[3] = {"07/15 as 06:51 PM", "07/18 as 06:52 PM", "07/22 as 12:22 PM"};
  const char * msgs[3] = {"Lote iniciado.", "Avancou automaticamente para a etapa: Maturacao", "Avancou automaticamente para a etapa: Etapa 3"};
  uint32_t dot_color[3] = {0x666666, 0xa855f7, 0xa855f7};

  for(int i=0; i<3; i++) {
    lv_obj_t * lc = lv_obj_create(t3);
    lv_obj_set_size(lc, 390, 60);
    lv_obj_set_pos(lc, 50, 45 + i*70);
    lv_obj_set_style_bg_color(lc, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(lc, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(lc, 12, 0);
    lv_obj_set_style_pad_all(lc, 0, 0);
    lv_obj_clear_flag(lc, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * ldot = lv_obj_create(t3);
    lv_obj_set_size(ldot, 12, 12);
    lv_obj_set_pos(ldot, 20, 45 + i*70 + 24);
    lv_obj_set_style_bg_color(ldot, lv_color_hex(dot_color[i]), 0);
    lv_obj_set_style_border_color(ldot, lv_color_hex(0x050505), 0);
    lv_obj_set_style_border_width(ldot, 2, 0);
    lv_obj_set_style_radius(ldot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_all(ldot, 0, 0);
    lv_obj_clear_flag(ldot, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * b_bg = lv_obj_create(lc);
    lv_obj_set_size(b_bg, 80, 20);
    lv_obj_align(b_bg, LV_ALIGN_TOP_LEFT, 5, 5);
    lv_obj_set_style_bg_color(b_bg, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_width(b_bg, 0, 0);
    lv_obj_set_style_radius(b_bg, 4, 0);
    lv_obj_set_style_pad_all(b_bg, 0, 0);
    lv_obj_clear_flag(b_bg, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * b_txt = lv_label_create(b_bg);
    lv_label_set_text(b_txt, badges[i]);
    lv_obj_set_style_text_font(b_txt, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(b_txt, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(b_txt);

    lv_obj_t * dt = lv_label_create(lc);
    lv_label_set_text(dt, dates[i]);
    lv_obj_set_style_text_font(dt, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(dt, lv_color_hex(0x666666), 0);
    lv_obj_align(dt, LV_ALIGN_TOP_RIGHT, -5, 8);

    lv_obj_t * msg = lv_label_create(lc);
    lv_label_set_text(msg, msgs[i]);
    lv_obj_set_style_text_font(msg, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(msg, lv_color_hex(0xDDDDDD), 0);
    lv_obj_align(msg, LV_ALIGN_BOTTOM_LEFT, 5, -5);
  }
}

void log_row_event_cb(lv_event_t * e) {
  lv_obj_t * tv = (lv_obj_t *)lv_event_get_user_data(e);
  lv_obj_set_tile_id(tv, 1, 0, LV_ANIM_ON);
}

void build_app_fermentation() {
  if (!isRampActive) {
      build_app_wizard();
      return;
  }

  lv_label_set_text(dots, LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET); 

  lv_obj_t * tv = lv_tileview_create(content_view);
  lv_obj_set_size(tv, 450, 240);
  lv_obj_remove_style(tv, NULL, LV_PART_SCROLLBAR);
  lv_obj_set_style_bg_color(tv, lv_color_hex(0x0a0a0a), 0);
  lv_obj_set_style_bg_opa(tv, 255, 0);
  lv_obj_set_style_anim_time(tv, 150, 0);
  lv_obj_set_style_border_width(tv, 0, 0);

  lv_obj_t * t0 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
  lv_obj_set_style_bg_opa(t0, 0, 0);
  build_telemetry_page1(t0);

  lv_obj_t * t1 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_set_style_bg_opa(t1, 0, 0);
  build_telemetry_chart(t1);

  lv_obj_t * t2 = lv_tileview_add_tile(tv, 2, 0, LV_DIR_LEFT | LV_DIR_RIGHT);
  lv_obj_set_style_bg_opa(t2, 0, 0);
  build_fermentation_ramps(t2);

  lv_obj_t * t3 = lv_tileview_add_tile(tv, 3, 0, LV_DIR_LEFT);
  lv_obj_set_style_bg_opa(t3, 0, 0);
  build_fermentation_logs(t3);
}


// ----------------------------------------------------
// APP 3: LOGS / HISTORICO
// ----------------------------------------------------
// APP 3: GELADEIRA (Ex-Logs)
// ----------------------------------------------------
void build_app_logs() {
  lv_label_set_text(dots, "");

  lv_obj_t * cont = lv_obj_create(content_view);
  lv_obj_set_size(cont, 450, 240);
  lv_obj_center(cont);
  lv_obj_set_style_bg_color(cont, lv_color_hex(0x0a0a0a), 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0); // REMOVE DEFAULT PADDING
  lv_obj_clear_flag(cont, LV_OBJ_FLAG_SCROLLABLE);

  // Box Top-Left (Temp)
  lv_obj_t * c1 = lv_obj_create(cont);
  lv_obj_set_size(c1, 220, 115);
  lv_obj_align(c1, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(c1, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(c1, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_width(c1, 1, 0);
  lv_obj_set_style_radius(c1, 12, 0);
  lv_obj_clear_flag(c1, LV_OBJ_FLAG_SCROLLABLE);
  
  lv_obj_t * t1 = lv_label_create(c1);
  lv_label_set_text(t1, "TEMP GELADEIRA (S2)");
  lv_obj_set_style_text_font(t1, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(t1, lv_color_hex(0x888888), 0);
  lv_obj_align(t1, LV_ALIGN_TOP_LEFT, 0, 0);
  
  lbl_geladeira_s2 = lv_label_create(c1);
  lv_label_set_text(lbl_geladeira_s2, "--");
  lv_obj_set_style_text_font(lbl_geladeira_s2, &lv_font_montserrat_extrabold_48, 0);
  lv_obj_set_style_text_color(lbl_geladeira_s2, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_geladeira_s2, LV_ALIGN_BOTTOM_LEFT, 0, 5);

  lbl_geladeira_s2_unit = lv_label_create(c1);
  lv_label_set_text(lbl_geladeira_s2_unit, "C");
  lv_obj_set_style_text_font(lbl_geladeira_s2_unit, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_geladeira_s2_unit, lv_color_hex(0x888888), 0);
  lv_obj_align_to(lbl_geladeira_s2_unit, lbl_geladeira_s2, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);

  // Box Top-Right (Umidade)
  lv_obj_t * c2 = lv_obj_create(cont);
  lv_obj_set_size(c2, 220, 115);
  lv_obj_align(c2, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(c2, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(c2, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_width(c2, 1, 0);
  lv_obj_set_style_radius(c2, 12, 0);
  lv_obj_clear_flag(c2, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t * t2 = lv_label_create(c2);
  lv_label_set_text(t2, "UMIDADE");
  lv_obj_set_style_text_font(t2, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(t2, lv_color_hex(0x888888), 0);
  lv_obj_align(t2, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_geladeira_hum = lv_label_create(c2);
  lv_label_set_text(lbl_geladeira_hum, "--%");
  lv_obj_set_style_text_font(lbl_geladeira_hum, &lv_font_montserrat_extrabold_48, 0);
  lv_obj_set_style_text_color(lbl_geladeira_hum, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_geladeira_hum, LV_ALIGN_BOTTOM_LEFT, 0, 5);

  // Box Bottom-Left (Setpoint)
  lv_obj_t * c3 = lv_obj_create(cont);
  lv_obj_set_size(c3, 220, 115);
  lv_obj_align(c3, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_set_style_bg_color(c3, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(c3, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_width(c3, 1, 0);
  lv_obj_set_style_radius(c3, 12, 0);
  lv_obj_clear_flag(c3, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(c3, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(c3, setpoint_click_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t * t3 = lv_label_create(c3);
  lv_label_set_text(t3, "SETPOINT");
  lv_obj_set_style_text_font(t3, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(t3, lv_color_hex(0x888888), 0);
  lv_obj_align(t3, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_geladeira_sp = lv_label_create(c3);
  lv_label_set_text(lbl_geladeira_sp, "--");
  lv_obj_set_style_text_font(lbl_geladeira_sp, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lbl_geladeira_sp, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_geladeira_sp, LV_ALIGN_BOTTOM_LEFT, 0, 5);
  
  lbl_geladeira_sp_unit = lv_label_create(c3);
  lv_label_set_text(lbl_geladeira_sp_unit, "C");
  lv_obj_set_style_text_font(lbl_geladeira_sp_unit, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_geladeira_sp_unit, lv_color_hex(0x888888), 0);
  lv_obj_align_to(lbl_geladeira_sp_unit, lbl_geladeira_sp, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);

  // Box Bottom-Right (Uptime)
  lv_obj_t * c4 = lv_obj_create(cont);
  lv_obj_set_size(c4, 220, 115);
  lv_obj_align(c4, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(c4, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(c4, lv_color_hex(0x222222), 0);
  lv_obj_set_style_border_width(c4, 1, 0);
  lv_obj_set_style_radius(c4, 12, 0);
  lv_obj_clear_flag(c4, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t * t4 = lv_label_create(c4);
  lv_label_set_text(t4, "UPTIME");
  lv_obj_set_style_text_font(t4, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(t4, lv_color_hex(0x888888), 0);
  lv_obj_align(t4, LV_ALIGN_TOP_LEFT, 0, 0);

  lbl_geladeira_uptime = lv_label_create(c4);
  lv_label_set_text(lbl_geladeira_uptime, "--");
  lv_obj_set_style_text_font(lbl_geladeira_uptime, &lv_font_montserrat_32, 0);
  lv_obj_set_style_text_color(lbl_geladeira_uptime, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(lbl_geladeira_uptime, LV_ALIGN_BOTTOM_LEFT, 0, 5);
}

// ----------------------------------------------------
// APP 4: SETTINGS (CONFIGURAÇÕES)
// ----------------------------------------------------
// Retorna struct customizada ou ponteiros? Vamos retornar o label `v` e tornar o box clicavel se passarmos um callback!
lv_obj_t* create_input_group(lv_obj_t * parent, int x, int y, int w, const char * lbl, const char * val, void (*click_cb)(lv_event_t*) = NULL) {
    lv_obj_t * c = lv_obj_create(parent);
    lv_obj_set_size(c, w, 50);
    lv_obj_set_pos(c, x, y);
    lv_obj_set_style_bg_opa(c, 0, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    
    lv_obj_t * l = lv_label_create(c);
    lv_label_set_text(l, lbl);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(0x666666), 0);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
    
    lv_obj_t * box = lv_obj_create(c);
    lv_obj_set_size(box, w, 32);
    lv_obj_align(box, LV_ALIGN_BOTTOM_LEFT, 0, 0);
    lv_obj_set_style_bg_color(box, lv_color_hex(0x000000), 0);
    lv_obj_set_style_border_color(box, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lv_obj_set_style_radius(box, 6, 0);
    
    if (click_cb != NULL) {
        lv_obj_add_flag(box, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(box, click_cb, LV_EVENT_CLICKED, NULL);
    }
    
    lv_obj_t * v = lv_label_create(box);
    lv_label_set_text(v, val);
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(v, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(v, LV_ALIGN_LEFT_MID, 10, 0);
    
    return v;
}

lv_obj_t* create_set_btn(lv_obj_t * parent, int x, int y, int w, int h, uint32_t bg_hex, uint32_t txt_hex, const char * txt) {
    lv_obj_t * btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(bg_hex), 0);
    lv_obj_set_style_radius(btn, 6, 0);
    
    lv_obj_t * l = lv_label_create(btn);
    lv_label_set_text(l, txt);
    lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(txt_hex), 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_center(l);
    return btn;
}

lv_obj_t* create_set_card(lv_obj_t * parent, int y, int h, const char * title) {
    lv_obj_t * card = lv_obj_create(parent);
    lv_obj_set_size(card, 450, h);
    lv_obj_set_pos(card, 0, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x0f0f0f), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t * t = lv_label_create(card);
    lv_label_set_text(t, title);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_letter_space(t, 1, 0);
    lv_obj_set_pos(t, 20, 20);
    
    return card;
}

static void btn_open_wifi_wizard_cb(lv_event_t * e) {
    is_bypassing_wizard = true; // garante que ao conectar ele volte pro launcher (sem seguir pro pair)
    start_wifi_scan();
    lv_scr_load_anim(scr_wiz_wifi, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
}

void build_app_settings() {
  lv_label_set_text(dots, ""); // Sem dots na pagina de config
  
  lv_obj_t * page = lv_obj_create(content_view);
  lv_obj_set_size(page, 450, 240); // Aumentar altura? Não, a altura da area visivel eh 255. O conteudo interno vai fazer scroll
  lv_obj_align(page, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_color(page, lv_color_hex(0x0a0a0a), 0); // Fundo sólido para acelerar renderização
  lv_obj_set_style_bg_opa(page, 255, 0); 
  lv_obj_set_style_border_width(page, 0, 0);
  lv_obj_set_style_pad_all(page, 0, 0);
  lv_obj_remove_style(page, NULL, LV_PART_SCROLLBAR); // hide scrollbar
  lv_obj_clear_flag(page, LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_MOMENTUM); // Remove física de mola para poupar CPU
  
  // Wrapper pra conter tudo e forcar scroll
  lv_obj_t * wrapper = lv_obj_create(page);
  lv_obj_set_width(wrapper, 450);
  lv_obj_set_style_bg_color(wrapper, lv_color_hex(0x0a0a0a), 0); // Fundo sólido para acelerar renderização
  lv_obj_set_style_bg_opa(wrapper, 255, 0);
  lv_obj_set_style_border_width(wrapper, 0, 0);
  lv_obj_set_style_pad_all(wrapper, 0, 0);
  lv_obj_clear_flag(wrapper, LV_OBJ_FLAG_SCROLLABLE);

  int y = 0;


  // 0. MODO DE OPERACAO
  lv_obj_t * c0 = create_set_card(wrapper, y, 130, "MODO DE OPERACAO");
  lv_obj_t * d0 = lv_label_create(c0);
  lv_label_set_text(d0, "Selecione o perfil de controle térmico do equipamento.");
  lv_obj_set_style_text_font(d0, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(d0, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(d0, 20, 40);
  
  btn_mode_ferm = create_set_btn(c0, 10, 70, 95, 40, 0xFFFFFF, 0x000000, "FERMENT.");
  lv_obj_add_event_cb(btn_mode_ferm, mode_btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)MODE_FERM);
  
  btn_mode_geladeira = create_set_btn(c0, 115, 70, 95, 40, 0xFFFFFF, 0x000000, "GELADEIRA");
  lv_obj_add_event_cb(btn_mode_geladeira, mode_btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)MODE_GELADEIRA);
  
  btn_mode_chopp = create_set_btn(c0, 220, 70, 95, 40, 0xFFFFFF, 0x000000, "CHOPP");
  lv_obj_add_event_cb(btn_mode_chopp, mode_btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)MODE_CHOPP);
  
  btn_mode_clean = create_set_btn(c0, 325, 70, 95, 40, 0xFFFFFF, 0x000000, "OFF");
  lv_obj_add_event_cb(btn_mode_clean, mode_btn_event_cb, LV_EVENT_CLICKED, (void*)(uintptr_t)MODE_CLEAN);
  
  update_mode_ui();
  
  y += 130 + 20;

  // 1. DENSIDADE
  lv_obj_t * c1 = create_set_card(wrapper, y, 100, "CONFIGURACOES DE DENSIDADE / GRAVIDADE");
  create_input_group(c1, 20, 40, 270, "NOME SENSOR SG", "iSpindel");
  create_set_btn(c1, 300, 54, 130, 32, 0xFFFFFF, 0x000000, "SALVAR NOME SG");
  y += 100 + 20;

  // 2. CALIBRACAO
  lv_obj_t * c2 = create_set_card(wrapper, y, 130, "CALIBRACAO (OFFSET)");
  lv_obj_t * d2 = lv_label_create(c2);
  lv_label_set_text(d2, "Ajuste fino adicionado a leitura bruta dos sensores. Use valores positivos ou negativos (ex: -0.5).");
  lv_obj_set_style_text_font(d2, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(d2, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(d2, 20, 40);
  create_input_group(c2, 20, 70, 80, "OFFSET S1 (C)", "0");
  create_input_group(c2, 110, 70, 80, "OFFSET S2 (C)", "0");
  create_input_group(c2, 200, 70, 80, "OFFSET SG", "0");
  create_set_btn(c2, 290, 84, 140, 32, 0xFFFFFF, 0x000000, "SALVAR CALIBRACAO");
  y += 130 + 20;

  // 3. PID
  lv_obj_t * c3 = create_set_card(wrapper, y, 260, "PARAMETROS PID");
  // AQUECIMENTO
  lv_obj_t * h3a = lv_label_create(c3);
  lv_label_set_text(h3a, "AQUECIMENTO (RESISTENCIA)");
  lv_obj_set_style_text_font(h3a, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(h3a, lv_color_hex(0x888888), 0);
  lv_obj_set_pos(h3a, 20, 50);
  
  lv_obj_t * btn_tune1 = create_set_btn(c3, 290, 41, 140, 32, 0xFFFFFF, 0x000000, "INICIAR AUTO-TUNE");
  lv_obj_add_event_cb(btn_tune1, autotune_btn_event_cb, LV_EVENT_CLICKED, NULL);

  char buf[16];
  sprintf(buf, "%.1f", pidKp);
  lbl_pid_kp = create_input_group(c3, 20, 80, 100, "KP", buf, kp_click_cb);
  sprintf(buf, "%.1f", pidKi);
  lbl_pid_ki = create_input_group(c3, 130, 80, 100, "KI", buf, ki_click_cb);
  sprintf(buf, "%.1f", pidKd);
  lbl_pid_kd = create_input_group(c3, 240, 80, 100, "KD", buf, kd_click_cb);
  sprintf(buf, "%d", pwmWindow);
  lbl_pid_pwm = create_input_group(c3, 350, 80, 80, "JANELA PWM", buf, pwm_click_cb);

  // REFRIGERACAO
  lv_obj_t * h3b = lv_label_create(c3);
  lv_label_set_text(h3b, "REFRIGERACAO (COMPRESSOR)");
  lv_obj_set_style_text_font(h3b, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(h3b, lv_color_hex(0x888888), 0);
  lv_obj_set_pos(h3b, 20, 150);

  lv_obj_t * btn_tune2 = create_set_btn(c3, 290, 141, 140, 32, 0xFFFFFF, 0x000000, "INICIAR AUTO-TUNE");

  create_input_group(c3, 20, 180, 100, "KP", "40000");
  create_input_group(c3, 130, 180, 100, "KI", "200");
  create_input_group(c3, 240, 180, 100, "KD", "0");

  create_set_btn(c3, 310, 215, 120, 32, 0xFFFFFF, 0x000000, "ATUALIZAR PID");
  y += 260 + 20;

  // 4. PUSH
  lv_obj_t * c4 = create_set_card(wrapper, y, 130, "NOTIFICACOES PUSH");
  lv_obj_t * d4 = lv_label_create(c4);
  lv_label_set_text(d4, "Receba alertas sobre finalizacao de rampas, alvo atingido, estabilizacao de gravidade (FG) e erros.");
  lv_obj_set_style_text_font(d4, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(d4, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(d4, 20, 40);
  
  lv_obj_t * p_box = lv_obj_create(c4);
  lv_obj_set_size(p_box, 410, 50);
  lv_obj_set_pos(p_box, 20, 65);
  lv_obj_set_style_bg_color(p_box, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(p_box, lv_color_hex(0x222222), 0);
  lv_obj_set_style_radius(p_box, 8, 0);
  lv_obj_set_style_pad_all(p_box, 0, 0);

  lv_obj_t * p_lbl1 = lv_label_create(p_box);
  lv_label_set_text(p_lbl1, "Status do Dispositivo");
  lv_obj_set_style_text_font(p_lbl1, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(p_lbl1, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_pos(p_lbl1, 10, 10);

  lv_obj_t * p_lbl2 = lv_label_create(p_box);
  lv_label_set_text(p_lbl2, "Este dispositivo esta inscrito e recebera os alertas.");
  lv_obj_set_style_text_font(p_lbl2, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(p_lbl2, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(p_lbl2, 10, 28);
  
  create_set_btn(p_box, 310, 10, 90, 30, 0x10b981, 0xFFFFFF, "ATIVADO");
  y += 130 + 20;

  // 5. RELES
  lv_obj_t * c5 = create_set_card(wrapper, y, 120, "TESTE DE RELES");
  create_set_btn(c5, 20, 45, 130, 32, 0xFFFFFF, 0x000000, "AQUECER");
  create_set_btn(c5, 160, 45, 130, 32, 0xFFFFFF, 0x000000, "REFRIGERAR");
  create_set_btn(c5, 300, 45, 130, 32, 0xFFFFFF, 0x000000, "AUTO");
  lv_obj_t * d5 = lv_label_create(c5);
  lv_label_set_text(d5, "* O modo AUTO retorna o controle para o algoritmo PID.");
  lv_obj_set_style_text_font(d5, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(d5, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(d5, 20, 90);
  y += 120 + 20;

  // 6. 1-WIRE
  lv_obj_t * c6 = create_set_card(wrapper, y, 120, "MAPEAMENTO DE SENSORES (1-WIRE)");
  create_set_btn(c6, 20, 45, 180, 36, 0x8b5cf6, 0xFFFFFF, "ESCANEAR SENSORES");
  create_set_btn(c6, 210, 45, 180, 36, 0x10b981, 0xFFFFFF, "SALVAR MAPEAMENTO");
  lv_obj_t * d6 = lv_label_create(c6);
  lv_label_set_text(d6, "Nenhum sensor escaneado. Clique em \"Escanear Sensores\" para buscar na placa.");
  lv_obj_set_style_text_font(d6, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(d6, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(d6, 20, 90);
  y += 120 + 20;

  // 7. SISTEMA
  lv_obj_t * c7 = create_set_card(wrapper, y, 130, "SISTEMA");
  create_set_btn(c7, 20, 45, 200, 32, 0xFFFFFF, 0x000000, "REINICIAR");
  create_set_btn(c7, 230, 45, 200, 32, 0xFFFFFF, 0x000000, "BAIXAR LOGS");
  create_set_btn(c7, 20, 85, 200, 32, 0xFFFFFF, 0x000000, "LIMPAR LOGS");
  create_set_btn(c7, 230, 85, 200, 32, 0x2E1015, 0xf43f5e, "RESET FABRICA");
  y += 130 + 20;

  
  // 8. REDE WI-FI
  lv_obj_t * c_wifi = create_set_card(wrapper, y, 100, "REDE WI-FI");
  lv_obj_t * btn_wifi = create_set_btn(c_wifi, 20, 45, 200, 32, 0xFFFFFF, 0x000000, "ALTERAR WI-FI");
  lv_obj_add_event_cb(btn_wifi, btn_open_wifi_wizard_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t * d_wifi = lv_label_create(c_wifi);
  lv_label_set_text(d_wifi, "O sistema salva a rede atual e tenta reconectar automaticamente.");
  lv_obj_set_style_text_font(d_wifi, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(d_wifi, lv_color_hex(0x666666), 0);
  lv_obj_set_pos(d_wifi, 230, 55);
  y += 100 + 20;

  // 9. OTA
  lv_obj_t * c8 = create_set_card(wrapper, y, 130, "ATUALIZACAO REMOTA (OTA)");
  lv_obj_t * o_box = lv_obj_create(c8);
  lv_obj_set_size(o_box, 410, 70);
  lv_obj_set_pos(o_box, 20, 45);
  lv_obj_set_style_bg_color(o_box, lv_color_hex(0x022c22), 0);
  lv_obj_set_style_border_color(o_box, lv_color_hex(0x064e3b), 0);
  lv_obj_set_style_radius(o_box, 8, 0);
  lv_obj_set_style_pad_all(o_box, 0, 0);

  lv_obj_t * o_lbl1 = lv_label_create(o_box);
  lv_label_set_text(o_lbl1, "Nova Versao Oficial Disponivel: v2.0.47");
  lv_obj_set_style_text_font(o_lbl1, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(o_lbl1, lv_color_hex(0x34d399), 0);
  lv_obj_set_pos(o_lbl1, 15, 15);

  lv_obj_t * o_lbl2 = lv_label_create(o_box);
  lv_label_set_text(o_lbl2, "Seu equipamento esta na versao V2.0.07 BETA STABLE.\nAtualize para obter as ultimas melhorias.");
  lv_obj_set_style_text_font(o_lbl2, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(o_lbl2, lv_color_hex(0x059669), 0);
  lv_obj_set_pos(o_lbl2, 15, 35);
  
  create_set_btn(o_box, 240, 20, 155, 30, 0x10b981, 0xFFFFFF, "INSTALAR AUTOMATICO");
  y += 130 + 40; // extra padding at bottom

  lv_obj_set_height(wrapper, y);
}

// ----------------------------------------------------
// APP 5: SOBRE
// ----------------------------------------------------

static void btn_about_wifi_cb(lv_event_t * e) {
    // Abre a tela 2 do Wizard (WiFi Scanner) e inicia varredura
    extern void start_wifi_scan();
    start_wifi_scan();
    lv_scr_load_anim(scr_wiz_wifi, LV_SCR_LOAD_ANIM_FADE_ON, 300, 0, false);
}

void build_app_about() {
  lv_label_set_text(dots, LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET); 

  lv_obj_t * tv = lv_tileview_create(content_view);
  lv_obj_set_size(tv, 450, 240);
  lv_obj_remove_style(tv, NULL, LV_PART_SCROLLBAR); 
  lv_obj_set_style_bg_color(tv, lv_color_hex(0x0a0a0a), 0);
  lv_obj_set_style_bg_opa(tv, 255, 0);
  lv_obj_set_style_anim_time(tv, 150, 0);
  lv_obj_set_style_border_width(tv, 0, 0);

  lv_obj_t * t0 = lv_tileview_add_tile(tv, 0, 0, LV_DIR_RIGHT);
  lv_obj_set_style_bg_opa(t0, 0, 0);

  lv_label_set_text(dots, ""); // Sem dots na pagina sobre

  // Botoes no topo direito
  lv_obj_t * btn_wifi = lv_btn_create(t0);
  lv_obj_set_size(btn_wifi, 140, 36);
  lv_obj_align(btn_wifi, LV_ALIGN_TOP_RIGHT, 0, 0);
  lv_obj_set_style_bg_color(btn_wifi, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(btn_wifi, 8, 0);
  lv_obj_t * l_wifi = lv_label_create(btn_wifi);
  lv_label_set_text(l_wifi, LV_SYMBOL_SETTINGS " CONFIGURAR WIFI");
  lv_obj_set_style_text_font(l_wifi, &lv_font_montserrat_10, 0);
  lv_obj_set_style_text_color(l_wifi, lv_color_hex(0x000000), 0);
  lv_obj_set_style_text_letter_space(l_wifi, 1, 0);
  lv_obj_center(l_wifi);
  lv_obj_add_event_cb(btn_wifi, btn_about_wifi_cb, LV_EVENT_CLICKED, NULL);



  const char * titles[6] = {"FIRMWARE", "HARDWARE", "UPTIME", "REDE (SSID)", "IP LOCAL", "SINAL WIFI"};
  
  String fw_str = "v2.0.07";
  String hw_str = String(ESP.getChipModel());
  if (hw_str == "") hw_str = "ESP32-S3"; // Fallback
  
  unsigned long t_up = millis() / 1000;
  int d = t_up / 86400;
  int h = (t_up % 86400) / 3600;
  int m = (t_up % 3600) / 60;
  char uptime_buf[32];
  if(d > 0) sprintf(uptime_buf, "%dd %dh", d, h);
  else sprintf(uptime_buf, "%dh %dm", h, m);
  
  String ssid_str = WiFi.SSID();
  if (ssid_str == "" || WiFi.status() != WL_CONNECTED) ssid_str = "Desconectado";
  
  String ip_str = WiFi.localIP().toString();
  if (ip_str == "0.0.0.0" || WiFi.status() != WL_CONNECTED) ip_str = "--";
  
  String rssi_str = String(WiFi.RSSI()) + "dBm";
  if (WiFi.status() != WL_CONNECTED) rssi_str = "--";

  String vals_str[6] = {fw_str, hw_str, String(uptime_buf), ssid_str, ip_str, rssi_str};
  uint32_t c[6] = {0xFFFFFF, 0x3b82f6, 0x22c55e, 0xFFFFFF, 0xFFFFFF, 0x10b981};

  for(int i=0; i<6; i++) {
    lv_obj_t * card = lv_obj_create(t0);
    lv_obj_set_size(card, 140, 90);
    lv_obj_set_pos(card, (i%3)*155, 40 + (i/3)*100);
    lv_obj_set_style_bg_color(card, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(card, lv_color_hex(0x222222), 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_t * t = lv_label_create(card);
    lv_label_set_text(t, titles[i]);
    lv_obj_set_style_text_font(t, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(t, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_letter_space(t, 1, 0);
    lv_obj_align(t, LV_ALIGN_TOP_MID, 0, 20);
    
    lv_obj_t * v = lv_label_create(card);
    lv_label_set_text(v, vals_str[i].c_str());
    lv_obj_set_style_text_font(v, &lv_font_montserrat_14, 0); 
    lv_obj_set_style_text_color(v, lv_color_hex(c[i]), 0);
    lv_obj_align(v, LV_ALIGN_BOTTOM_MID, 0, -25);
  }


  lv_obj_t * t1 = lv_tileview_add_tile(tv, 1, 0, LV_DIR_LEFT);
  lv_obj_set_style_bg_opa(t1, 0, 0);
  build_telemetry_relays(t1);
}

// ----------------------------------------------------
// GRADE DO LAUNCHER
// ----------------------------------------------------
void build_app_grid() {
  const int app_ids[6] = {2, 3, 0, 1, 4, 5};
  const char * apps[6]  = {"FERMENTADOR", "GELADEIRA", "CHOPPEIRA", "TELEMETRIA", "SETTINGS", "SOBRE"};
  const char * icons[6] = {LV_SYMBOL_LIST, LV_SYMBOL_HOME, LV_SYMBOL_TINT, LV_SYMBOL_WIFI, LV_SYMBOL_SETTINGS, LV_SYMBOL_WARNING};
  
  for(int i = 0; i < 6; i++) {
    lv_obj_t * btn = lv_btn_create(content_view);
    lv_obj_set_size(btn, 138, 110);
    
    int col = i % 3;
    int row = i / 3;
    lv_obj_set_pos(btn, col * 156, (row * 125)); 
    
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(0x222222), 0); 
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 16, 0); 
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
    
    lv_obj_add_event_cb(btn, btn_app_event_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)app_ids[i]);
    
    lv_obj_t * icon_box = lv_obj_create(btn);
    lv_obj_set_size(icon_box, 38, 38);
    lv_obj_align(icon_box, LV_ALIGN_TOP_MID, 0, 28); 
    lv_obj_set_style_bg_color(icon_box, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_border_color(icon_box, lv_color_hex(0x333333), 0);
    lv_obj_set_style_border_width(icon_box, 1, 0);
    lv_obj_set_style_radius(icon_box, 10, 0);
    lv_obj_set_style_pad_all(icon_box, 0, 0);
    lv_obj_clear_flag(icon_box, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t * icn = lv_label_create(icon_box);
    lv_label_set_text(icn, icons[i]);
    lv_obj_set_style_text_color(icn, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(icn);

    lv_obj_t * label = lv_label_create(btn);
    lv_label_set_text(label, apps[i]);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_10, 0); 
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_letter_space(label, 1, 0); 
    lv_obj_align(label, LV_ALIGN_BOTTOM_MID, 0, -28); 
  }
}

// ----------------------------------------------------
// TELA MESTRE
// ----------------------------------------------------
void build_launcher() {
  scr_launcher = lv_obj_create(NULL);
  lv_obj_set_style_bg_color(scr_launcher, lv_color_hex(0x050505), 0);
  
  add_wiz_header(scr_launcher);

  // BOTÃO FECHAR MENU (Voltar para Telemetria)
  header_close_menu_btn = lv_btn_create(scr_launcher);
  lv_obj_set_size(header_close_menu_btn, 36, 36);
  lv_obj_align(header_close_menu_btn, LV_ALIGN_TOP_RIGHT, -15, 8);
  lv_obj_set_style_bg_color(header_close_menu_btn, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(header_close_menu_btn, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(header_close_menu_btn, 1, 0);
  lv_obj_set_style_radius(header_close_menu_btn, 6, 0);
  lv_obj_add_event_cb(header_close_menu_btn, btn_close_menu_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t * c_lbl = lv_label_create(header_close_menu_btn);
  lv_label_set_text(c_lbl, LV_SYMBOL_LEFT);
  lv_obj_set_style_text_color(c_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(c_lbl);
  lv_obj_add_flag(header_close_menu_btn, LV_OBJ_FLAG_HIDDEN);

  // BOTÃO ABRIR MENU (Hamburguer)
  header_back_btn = lv_btn_create(scr_launcher);
  lv_obj_set_size(header_back_btn, 36, 36);
  lv_obj_align(header_back_btn, LV_ALIGN_TOP_RIGHT, -15, 8);
  lv_obj_set_style_bg_color(header_back_btn, lv_color_hex(0x1A1A1A), 0);
  lv_obj_set_style_border_color(header_back_btn, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(header_back_btn, 1, 0);
  lv_obj_set_style_radius(header_back_btn, 6, 0);
  lv_obj_add_event_cb(header_back_btn, btn_back_event_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t * hbb_lbl = lv_label_create(header_back_btn);
  lv_label_set_text(hbb_lbl, LV_SYMBOL_LIST);
  lv_obj_set_style_text_color(hbb_lbl, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(hbb_lbl);
  lv_obj_add_flag(header_back_btn, LV_OBJ_FLAG_HIDDEN); 

  // CRACHÁ CONEXÃO (Apenas Bolinha)
  badge = lv_obj_create(scr_launcher);
  lv_obj_set_size(badge, 12, 12);
  lv_obj_align(badge, LV_ALIGN_TOP_RIGHT, -65, 20); 
  lv_obj_set_style_bg_color(badge, lv_color_hex(0x10b981), 0);
  lv_obj_set_style_radius(badge, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(badge, 0, 0);
  lv_obj_clear_flag(badge, LV_OBJ_FLAG_SCROLLABLE);

  // DATA E HORA
  clock_txt = lv_label_create(scr_launcher);
  lv_label_set_text(clock_txt, "00:00:00");
  lv_obj_set_style_text_font(clock_txt, &lv_font_montserrat_14, 0); // Fonte 16px!
  lv_obj_set_style_text_color(clock_txt, lv_color_hex(0x888888), 0);
  lv_obj_align(clock_txt, LV_ALIGN_TOP_MID, 0, 18);

  // BOLINHAS DE PAGINAÇÃO
  dots = lv_label_create(scr_launcher);
  lv_label_set_text(dots, LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET " " LV_SYMBOL_BULLET);
  lv_obj_set_style_text_color(dots, lv_color_hex(0x444444), 0);
  lv_obj_align(dots, LV_ALIGN_BOTTOM_MID, 0, -2);
  lv_obj_add_flag(dots, LV_OBJ_FLAG_HIDDEN); 

  content_view = lv_obj_create(scr_launcher);
  lv_obj_set_size(content_view, 450, 240);
  lv_obj_align(content_view, LV_ALIGN_BOTTOM_MID, 0, -15); 
  lv_obj_set_style_bg_opa(content_view, 0, 0);
  lv_obj_set_style_border_width(content_view, 0, 0);
  lv_obj_set_style_pad_all(content_view, 0, 0);
  lv_obj_clear_flag(content_view, LV_OBJ_FLAG_SCROLLABLE);
  
  if (currentMode == MODE_FERM) {
      if (isRampActive) open_app(2);
      else btn_back_event_cb(NULL);
  } else if (currentMode == MODE_GELADEIRA) {
      open_app(3); // App 3 = Geladeira
  } else if (currentMode == MODE_CHOPP) {
      open_app(0); // App 0 = Choppeira
  } else if (currentMode == MODE_CLEAN) {
      btn_back_event_cb(NULL);
  }
}

#include <time.h>
void clock_timer_cb(lv_timer_t * timer) {
  if (clock_txt == NULL) return;
  
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  

  // Update badge color
  if (badge != NULL) {
      if (WiFi.status() == WL_CONNECTED && mqttClient.connected()) {
          lv_obj_set_style_bg_color(badge, lv_color_hex(0x10b981), 0);
      } else {
          lv_obj_set_style_bg_color(badge, lv_color_hex(0xEF4444), 0);
      }
  }

  if (timeinfo.tm_year < 120) { 
      static int h = 0;
      static int m = 0;
      static int s = 0;
      static unsigned long last_tick = 0;
      
      if (millis() - last_tick >= 1000) {
          s++;
          if (s >= 60) { s = 0; m++; }
          if (m >= 60) { m = 0; h++; }
          if (h >= 24) { h = 0; }
          last_tick = millis();
      }
      char buf[32];
      sprintf(buf, "%02d:%02d:%02d", h, m, s);
      lv_label_set_text(clock_txt, buf);
  } else {
      char buf[32];
      sprintf(buf, "%02d:%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
      lv_label_set_text(clock_txt, buf);
  }
}

void init_ui() {
    Serial.println("Inicializando LVGL (Interface Visual)...");
  Serial.begin(115200);
  Wire.begin(TOUCH_SDA, TOUCH_SCL);
  Wire.setClock(400000); // 400kHz I2C Fast Mode para ler o touch mais rápido
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);
  
  // Cria o semáforo e inicia a task no Core 0 (Processamento Paralelo)
  flush_sem = xSemaphoreCreateBinary();
  xTaskCreatePinnedToCore(flush_task, "flush_task", 4096, NULL, 1, NULL, 0);
  
  gfx->begin();

  lv_init();
  
  // Otimização Extrema: 
  // PSRAM tem alta latência para operações de desenho pixel a pixel (fontes, sombras).
  // Alocar o buffer na RAM INTERNA (SRAM) faz o LVGL renderizar até 10x mais rápido!
  uint32_t buf_size = SCREEN_WIDTH * SCREEN_HEIGHT / 5; // 1/5 da tela (aprox 60KB), cabe folgado na SRAM do S3
  buf = (lv_color_t *)heap_caps_malloc(buf_size * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  
  if (buf == NULL) {
      // Fallback de segurança
      buf_size = SCREEN_WIDTH * SCREEN_HEIGHT / 10;
      buf = (lv_color_t *)malloc(buf_size * sizeof(lv_color_t));
  }
  
  lv_disp_draw_buf_init(&draw_buf, buf, NULL, buf_size);

  static lv_disp_drv_t disp_drv;
  lv_disp_drv_init(&disp_drv);
  disp_drv.hor_res = SCREEN_WIDTH;
  disp_drv.ver_res = SCREEN_HEIGHT;
  disp_drv.flush_cb = my_disp_flush;
  disp_drv.draw_buf = &draw_buf;
  
  lv_disp_t * disp = lv_disp_drv_register(&disp_drv);
  
  // Aumentar a taxa de atualização (FPS) da tela de 30ms (padrão 33 FPS) para 10ms (100 FPS)
  if (disp != NULL && disp->refr_timer != NULL) {
      lv_timer_set_period(disp->refr_timer, 10);
  }

  static lv_indev_drv_t indev_drv;
  lv_indev_drv_init(&indev_drv);
  indev_drv.type = LV_INDEV_TYPE_POINTER;
  indev_drv.read_cb = my_touchpad_read;
  indev_drv.scroll_limit = 5; // Reduz a sensibilidade do scroll para responder mais rápido (o padrão é 10)
  
  lv_indev_t * indev = lv_indev_drv_register(&indev_drv);
  
  // Aumentar a frequência de leitura do touch de 30ms (padrão) para 15ms (~66 FPS)
  if (indev != NULL && indev->driver != NULL && indev->driver->read_timer != NULL) {
      lv_timer_set_period(indev->driver->read_timer, 15);
  }

  build_splash();
  build_wiz_welcome();
  build_wiz_wifi();
  build_wiz_pair();
  build_wiz_ready();
  build_launcher();
  
  lv_timer_create(clock_timer_cb, 1000, NULL);
  
  lv_scr_load(scr_splash);
}

bool booted = false;

void loop_ui() {
  lv_timer_handler(); 
  
  if (!booted && millis() > 4000) {
    booted = true;
    if(has_wifi_credentials()) {
      is_bypassing_wizard = true;
      wifi_setup_track_saved();
      if(scr_wiz_connecting == NULL) {
                    scr_wiz_connecting = lv_obj_create(NULL);
          lv_obj_set_style_bg_color(scr_wiz_connecting, lv_color_hex(0x050505), 0);
          
          lbl_connecting = lv_label_create(scr_wiz_connecting);
          lv_label_set_text(lbl_connecting, "Conectando ao Wi-Fi");
          lv_obj_set_style_text_font(lbl_connecting, &lv_font_montserrat_24, 0);
          lv_obj_set_style_text_color(lbl_connecting, lv_color_hex(0xFFFFFF), 0);
          lv_obj_align(lbl_connecting, LV_ALIGN_CENTER, 0, -20);
          
          lbl_connecting_ssid = lv_label_create(scr_wiz_connecting);
          lv_label_set_text(lbl_connecting_ssid, "");
          lv_obj_set_style_text_font(lbl_connecting_ssid, &lv_font_montserrat_12, 0);
          lv_obj_set_style_text_color(lbl_connecting_ssid, lv_color_hex(0x666666), 0);
          lv_obj_align(lbl_connecting_ssid, LV_ALIGN_CENTER, 0, 10);
      }
      lv_label_set_text(lbl_connecting, "Conectando ao Wi-Fi");
      lv_obj_set_style_text_color(lbl_connecting, lv_color_hex(0xFFFFFF), 0);
      if(lbl_connecting_ssid) lv_label_set_text(lbl_connecting_ssid, savedSSID.c_str());
      lv_scr_load_anim(scr_wiz_connecting, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    } else {
      lv_scr_load_anim(scr_wiz_welcome, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    }
  }
  
  static unsigned long wifi_connected_time = 0;
  static bool wifi_success_shown = false;

  if(scr_wiz_connecting != NULL && lv_scr_act() == scr_wiz_connecting) {
      WifiSetupState ws = wifi_setup_poll();
      if(ws == WIFI_SETUP_CONNECTED) {
          if (!wifi_success_shown) {
              wifi_success_shown = true;
              wifi_connected_time = millis();
              lv_label_set_text(lbl_connecting, "Conectado a rede:");
              lv_obj_set_style_text_color(lbl_connecting, lv_color_hex(0xFFFFFF), 0);
          } else if (millis() - wifi_connected_time > 3000) {
              wifi_success_shown = false; // reset for future use
              wifi_setup_reset();
              if(is_bypassing_wizard) {
                  lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_NONE, 150, 0, false);
              } else {
                  lv_scr_load_anim(scr_wiz_pair, LV_SCR_LOAD_ANIM_NONE, 150, 0, false);
              }
          }
      } else if (ws == WIFI_SETUP_FAILED) {
          wifi_success_shown = false;
          wifi_setup_reset();
          if (!wifi_setup_is_new_network()) {
              // Rede salva nao respondeu no boot (roteador offline/luz caiu): credenciais mantidas,
              // vai pro launcher e o ESP segue tentando reconectar sozinho
              lv_scr_load_anim(scr_launcher, LV_SCR_LOAD_ANIM_NONE, 150, 0, false);
          } else {
              // Rede nova falhou: wifi_setup ja apagou a senha e reiniciou a busca
              lv_scr_load_anim(scr_wiz_wifi, LV_SCR_LOAD_ANIM_NONE, 150, 0, false);
          }
      }
  }

  delay(5);
}
void ui_update_temperatures() {
    if (request_dashboard_refresh) {
        request_dashboard_refresh = false;
        unsigned long tUi = millis();
        if (currentMode == 0) {
            if (isRampActive) open_app(2);
            else btn_back_event_cb(NULL);
        } else if (currentMode == 1) {
            open_app(3);
        } else {
            open_app(0);
        }
        force_ui_update = true;
        Serial.printf("[UI] Tela do modo %d montada em %lu ms\n", currentMode, millis() - tUi);
    }

    static float last_temp = -999.0;
    static float last_setpoint = -999.0;
    static float last_gravity = -999.0;

    if(lbl_temp != NULL && arc_temp != NULL) {
        if(force_ui_update || currentTemp != last_temp) {
            char buf[16];
            sprintf(buf, "%.1f", currentTemp);
            if(lbl_telemetry_temp_unit != NULL) lv_obj_align_to(lbl_telemetry_temp_unit, lbl_telemetry_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);
            lv_label_set_text(lbl_temp, buf);
            lv_arc_set_value(arc_temp, (int)(currentTemp * 10));
            last_temp = currentTemp;
        }
    } else {
        last_temp = -999.0;
    }

    if(lbl_setpoint != NULL) {
        if(force_ui_update || currentSetpoint != last_setpoint) {
            char buf[16];
            sprintf(buf, "%.1f", currentSetpoint);
            lv_label_set_text(lbl_setpoint, buf);
            last_setpoint = currentSetpoint;
        }
    } else {
        last_setpoint = -999.0;
    }

    bool ispindelStale = false;
    uint32_t nowEpoch = time(NULL);
    
    // Se o NTP ja sincronizou (ano > 2000) e temos um epoch gravado
    if (nowEpoch > 1000000000 && lastIspindelEpoch > 0) {
        if (nowEpoch - lastIspindelEpoch > 7200) {
            ispindelStale = true;
        }
    } else {
        // Fallback pro millis() se NTP n sincronizou ou se n temos epoch
        if (lastIspindelUpdate > 0 && (millis() - lastIspindelUpdate > 7200000)) {
            ispindelStale = true;
        }
    }
    
    if(lbl_gravity != NULL) {
        if(ispindelStale) {
            if(last_gravity != -888.0) {
                lv_label_set_text(lbl_gravity, "ERROR");
                last_gravity = -888.0;
            }
        } else if(ispindelGravity > 0) {
            if(force_ui_update || ispindelGravity != last_gravity) {
                char buf[16];
                sprintf(buf, "%.3f", ispindelGravity);
                lv_label_set_text(lbl_gravity, buf);
                last_gravity = ispindelGravity;
                ui_update_ispindel_chart();
            }
        }
    } else {
        last_gravity = -999.0;
    }
    
    // Updates for Telemetry App
    static float last_tel_temp = -999.0;
    static float last_tel_sg = -999.0;
    
    if(lbl_telemetry_temp != NULL) {
        if(force_ui_update || currentTemp != last_tel_temp) {
            char buf[16];
            sprintf(buf, "%.1f", currentTemp);
            lv_label_set_text(lbl_telemetry_temp, buf);
            lv_obj_update_layout(lbl_telemetry_temp);
            if(lbl_telemetry_temp_unit != NULL) lv_obj_align_to(lbl_telemetry_temp_unit, lbl_telemetry_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);

            last_tel_temp = currentTemp;
        }
    } else {
        last_tel_temp = -999.0;
    }
    
    if(lbl_telemetry_sg != NULL) {
        if(ispindelStale) {
            if(last_tel_sg != -888.0) {
                lv_label_set_text(lbl_telemetry_sg, "ERR");
                last_tel_sg = -888.0;
            }
        } else if(ispindelGravity > 0) {
            if(force_ui_update || ispindelGravity != last_tel_sg) {
                char buf[16];
                sprintf(buf, "%.3f", ispindelGravity);
                lv_label_set_text(lbl_telemetry_sg, buf);
                last_tel_sg = ispindelGravity;
            }
        }
    } else {
        last_tel_sg = -999.0;
    }
    
        static float last_tel_t2 = -999.0;
    if(lbl_telemetry_t2 != NULL && currentTemp2 > -50.0) {
        if(force_ui_update || currentTemp2 != last_tel_t2) {
            char buf[16];
            sprintf(buf, "%.1f", currentTemp2);
            lv_label_set_text(lbl_telemetry_t2, buf);
            lv_obj_update_layout(lbl_telemetry_t2);
            if(lbl_telemetry_t2_unit != NULL) lv_obj_align_to(lbl_telemetry_t2_unit, lbl_telemetry_t2, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -3);
            last_tel_t2 = currentTemp2;
        }
    } else { last_tel_t2 = -999.0; }
    
    static float last_tel_hum = -999.0;
    if(lbl_telemetry_hum != NULL && currentHumidity > -50.0) {
        if(force_ui_update || currentHumidity != last_tel_hum) {
            char buf[16];
            sprintf(buf, "%.1f%%", currentHumidity);
            lv_label_set_text(lbl_telemetry_hum, buf);
            last_tel_hum = currentHumidity;
        }
    } else {
        if (lbl_telemetry_hum != NULL && last_tel_hum != -998.0) lv_label_set_text(lbl_telemetry_hum, "--%");
        last_tel_hum = -998.0;
    }
    
    static float last_tel_sp = -999.0;
    if(lbl_telemetry_setpoint != NULL) {
        if(force_ui_update || currentSetpoint != last_tel_sp) {
            char buf[16];
            sprintf(buf, "%.1f", currentSetpoint);
            lv_label_set_text(lbl_telemetry_setpoint, buf);
            lv_obj_update_layout(lbl_telemetry_setpoint);
            if(lbl_telemetry_setpoint_unit != NULL) lv_obj_align_to(lbl_telemetry_setpoint_unit, lbl_telemetry_setpoint, LV_ALIGN_OUT_RIGHT_BOTTOM, 3, -3);
            last_tel_sp = currentSetpoint;
        }
    } else { last_tel_sp = -999.0; }
    
    static float last_tel_fg = -999.0;
    if(lbl_telemetry_fg != NULL && finalGravity > 0) {
        if(force_ui_update || finalGravity != last_tel_fg) {
            char buf[16];
            sprintf(buf, "%.3f", finalGravity);
            lv_label_set_text(lbl_telemetry_fg, buf);
            last_tel_fg = finalGravity;
        }
    } else { last_tel_fg = -999.0; }
    
    static int last_heater_state = -1;
    if(lbl_telemetry_heater != NULL) {
        if(force_ui_update || (int)isHeaterOn != last_heater_state) {
            lv_label_set_text(lbl_telemetry_heater, isHeaterOn ? "LIGADO" : "DESLIGADO");
            last_heater_state = (int)isHeaterOn;
        }
    } else { last_heater_state = -1; }
    
    static int last_cooler_state = -1;
    if(lbl_telemetry_cooler != NULL) {
        if(force_ui_update || (int)isCoolerOn != last_cooler_state) {
            lv_label_set_text(lbl_telemetry_cooler, isCoolerOn ? "LIGADO" : "DESLIGADO");
            last_cooler_state = (int)isCoolerOn;
        }
    } else { last_cooler_state = -1; }
    
    // PWM Dinamico calculado via PID
    static int last_pwm_val = -1;
    if(lbl_telemetry_pwm != NULL) {
        // Converte o output do PID (ex: 0 a 2000ms) para 0-100%
        int current_pwm = (int)((pidOutput / pwmWindow) * 100.0);
        if(current_pwm < 0) current_pwm = 0;
        if(current_pwm > 100) current_pwm = 100;
        
        if(force_ui_update || current_pwm != last_pwm_val) {
            char buf[10];
            sprintf(buf, "%d%%", current_pwm);
            lv_label_set_text(lbl_telemetry_pwm, buf);
            last_pwm_val = current_pwm;
        }
    } else { last_pwm_val = -1; }
    
    // Uptime
    static unsigned long last_uptime_upd = 0;
    if(lbl_telemetry_uptime != NULL && (force_ui_update || millis() - last_uptime_upd > 60000 || last_uptime_upd == 0)) {
        unsigned long up = millis() / 1000;
        int m = (up / 60) % 60;
        int h = (up / 3600);
        char buf[16];
        if (h > 0) sprintf(buf, "%dh %dm", h, m);
        else sprintf(buf, "%dm", m);
        lv_label_set_text(lbl_telemetry_uptime, buf);
        last_uptime_upd = millis();
    }
    
    // RAM
    static unsigned long last_ram_upd = 0;
    if(lbl_telemetry_ram != NULL && (millis() - last_ram_upd > 5000 || last_ram_upd == 0)) {
        uint32_t freeHeap = ESP.getFreeHeap() / 1024;
        char buf[16];
        sprintf(buf, "%d KB", (int)freeHeap);
        lv_label_set_text(lbl_telemetry_ram, buf);
        last_ram_upd = millis();
    }

    // Updates for Choppeira
    static float last_chopp_temp = -999.0;
    if(lbl_chopp_temp != NULL && currentTemp2 > -50.0) {
        if(force_ui_update || currentTemp2 != last_chopp_temp) {
            char buf[16];
            sprintf(buf, "%.1f", currentTemp2);
            lv_label_set_text(lbl_chopp_temp, buf);
            if(lbl_chopp_temp_unit != NULL) lv_obj_align_to(lbl_chopp_temp_unit, lbl_chopp_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);
            last_chopp_temp = currentTemp2;
        }
    } else { last_chopp_temp = -999.0; }
    
    static float last_chopp_hum = -999.0;
    if(lbl_chopp_hum != NULL && currentHumidity >= 0) {
        if(force_ui_update || currentHumidity != last_chopp_hum) {
            char buf[16];
            sprintf(buf, "%.1f%%", currentHumidity);
            lv_label_set_text(lbl_chopp_hum, buf);
            last_chopp_hum = currentHumidity;
        }
    } else {
        if (lbl_chopp_hum != NULL && last_chopp_hum != -998.0) lv_label_set_text(lbl_chopp_hum, "--");
        last_chopp_hum = -998.0;
    }

    static float last_chopp_sp = -999.0;
    if(lbl_chopp_setpoint != NULL) {
        if(force_ui_update || currentSetpoint != last_chopp_sp) {
            char buf[16];
            sprintf(buf, "%.1f", currentSetpoint);
            lv_label_set_text(lbl_chopp_setpoint, buf);
            if(lbl_chopp_setpoint_unit != NULL) lv_obj_align_to(lbl_chopp_setpoint_unit, lbl_chopp_setpoint, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);
            last_chopp_sp = currentSetpoint;
        }
    } else { last_chopp_sp = -999.0; }
    // Updates for Geladeira
    static float last_gel_s1 = -999.0;
    if(lbl_geladeira_s1 != NULL) {
        if(force_ui_update || currentTemp != last_gel_s1) {
            char buf[16];
            sprintf(buf, "%.1f", currentTemp);
            if(lbl_telemetry_temp_unit != NULL) lv_obj_align_to(lbl_telemetry_temp_unit, lbl_telemetry_temp, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);
            lv_label_set_text(lbl_geladeira_s1, buf);
            last_gel_s1 = currentTemp;
        }
    } else { last_gel_s1 = -999.0; }

    static float last_gel_s2 = -999.0;
    if(lbl_geladeira_s2 != NULL && currentTemp2 > -50.0) {
        if(force_ui_update || currentTemp2 != last_gel_s2) {
            char buf[16];
            sprintf(buf, "%.1f", currentTemp2);
            lv_label_set_text(lbl_geladeira_s2, buf);
            lv_obj_update_layout(lbl_geladeira_s2);
            if(lbl_geladeira_s2_unit != NULL) lv_obj_align_to(lbl_geladeira_s2_unit, lbl_geladeira_s2, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);
            last_gel_s2 = currentTemp2;
        }
    } else { last_gel_s2 = -999.0; }

    static float last_gel_hum = -999.0;
    if(lbl_geladeira_hum != NULL && currentHumidity >= 0) {
        if(force_ui_update || currentHumidity != last_gel_hum) {
            char buf[16];
            sprintf(buf, "%.1f%%", currentHumidity);
            lv_label_set_text(lbl_geladeira_hum, buf);
            last_gel_hum = currentHumidity;
        }
    } else {
        if (lbl_geladeira_hum != NULL && last_gel_hum != -998.0) lv_label_set_text(lbl_geladeira_hum, "--%");
        last_gel_hum = -998.0;
    }
    
    static float last_gel_sp = -999.0;
    if(lbl_geladeira_sp != NULL) {
        if(force_ui_update || currentSetpoint != last_gel_sp) {
            char buf[16];
            sprintf(buf, "%.1f", currentSetpoint);
            lv_label_set_text(lbl_geladeira_sp, buf);
            lv_obj_update_layout(lbl_geladeira_sp);
            if(lbl_geladeira_sp_unit != NULL) lv_obj_align_to(lbl_geladeira_sp_unit, lbl_geladeira_sp, LV_ALIGN_OUT_RIGHT_BOTTOM, 5, -8);
            last_gel_sp = currentSetpoint;
        }
    } else { last_gel_sp = -999.0; }
    
    // Geladeira Uptime
    static unsigned long last_gel_uptime_upd = 0;
    if(lbl_geladeira_uptime != NULL && (force_ui_update || millis() - last_gel_uptime_upd > 60000 || last_gel_uptime_upd == 0)) {
        unsigned long up = millis() / 1000;
        int m = (up / 60) % 60;
        int h = (up / 3600);
        char buf[16];
        if(h > 0) sprintf(buf, "%dh %dm", h, m);
        else sprintf(buf, "%dm", m);
        lv_label_set_text(lbl_geladeira_uptime, buf);
        last_gel_uptime_upd = millis();
    }

    
    force_ui_update = false;
}


// --- OTA UI SCREENS ---
lv_obj_t * ota_screen = NULL;
lv_obj_t * ota_bar = NULL;
lv_obj_t * ota_label = NULL;

static lv_obj_t * ota_sub = NULL;
void ui_show_ota_screen() {
    ota_screen = lv_obj_create(lv_scr_act()); 
    lv_obj_set_size(ota_screen, 480, 320);
    lv_obj_set_style_bg_color(ota_screen, lv_color_hex(0x050505), 0);
    lv_obj_set_style_border_width(ota_screen, 0, 0);
    lv_obj_set_style_radius(ota_screen, 0, 0);
    lv_obj_set_style_pad_all(ota_screen, 0, 0);
    lv_obj_align(ota_screen, LV_ALIGN_CENTER, 0, 0);

    lv_obj_t * title = lv_label_create(ota_screen);
    lv_label_set_text(title, "Atualizando Sistema");
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
    lv_obj_align(title, LV_ALIGN_CENTER, 0, -80);

    lv_obj_t * sub = lv_label_create(ota_screen);
    ota_sub = sub;
    lv_label_set_text(sub, "Nao desligue o equipamento.\nO Breww esta instalando a nova versao.");
    lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, -30);

    ota_bar = lv_arc_create(ota_screen);
    lv_obj_set_size(ota_bar, 64, 64);
    lv_obj_align(ota_bar, LV_ALIGN_CENTER, 0, 50);
    lv_arc_set_rotation(ota_bar, 270);
    lv_arc_set_bg_angles(ota_bar, 0, 360);
    lv_arc_set_range(ota_bar, 0, 100);
    lv_arc_set_value(ota_bar, 0);
    lv_obj_remove_style(ota_bar, NULL, LV_PART_KNOB); // Remove bolinha
    lv_obj_clear_flag(ota_bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(ota_bar, 6, LV_PART_MAIN);
    lv_obj_set_style_arc_width(ota_bar, 6, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(ota_bar, lv_color_hex(0x222222), LV_PART_MAIN);
    lv_obj_set_style_arc_color(ota_bar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);

    ota_label = lv_label_create(ota_bar);
    lv_label_set_text(ota_label, "0%");
    lv_obj_set_style_text_color(ota_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(ota_label, &lv_font_montserrat_14, 0);
    lv_obj_align(ota_label, LV_ALIGN_CENTER, 0, 0);



    lv_refr_now(NULL); // desenha ja (o loop da tela fica parado durante o download)
}

static int ota_last_percent = -1;
void ui_update_ota_progress(int percent) {
    int& last_percent = ota_last_percent;
    static unsigned long lastDrawMs = 0;
    if (percent == last_percent) return;
    // Redesenhar a tela custa dezenas de ms: no maximo ~4x por segundo (100% sempre desenha)
    if (percent < 100 && millis() - lastDrawMs < 250) return;
    lastDrawMs = millis();
    last_percent = percent;

    if (ota_bar && ota_label) {
        lv_arc_set_value(ota_bar, percent);
        char buf[16];
        snprintf(buf, sizeof(buf), "%d%%", percent);
        lv_label_set_text(ota_label, buf);
        lv_timer_handler(); 
    }
}

void ui_ota_status(const char* text) {
    if (ota_sub) lv_label_set_text(ota_sub, text);
    ota_last_percent = -1;
    if (ota_bar) lv_arc_set_value(ota_bar, 0);
    if (ota_label) lv_label_set_text(ota_label, "0%");
    lv_refr_now(NULL);
}

void ui_hide_ota_screen() {
    if (ota_screen) lv_obj_del(ota_screen);
    ota_screen = NULL; ota_bar = NULL; ota_label = NULL; ota_sub = NULL;
    ota_last_percent = -1;
    lv_refr_now(NULL);
}

void ui_show_ota_success(bool success) {
    if (ota_screen) {
        lv_obj_clean(ota_screen); 
        
        lv_obj_t * title = lv_label_create(ota_screen);
        lv_obj_t * sub = lv_label_create(ota_screen);
        
        if (success) {
            lv_label_set_text(title, "Atualizado com Sucesso");
            lv_label_set_text(sub, "Reiniciando o painel para aplicar as alteracoes.");
        } else {
            lv_label_set_text(title, "Erro na Atualizacao");
            lv_label_set_text(sub, "Nao foi possivel baixar a nova versao.\nO Breww continua funcionando normalmente.");
        }
        
        lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
        lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
        lv_obj_align(title, LV_ALIGN_CENTER, 0, -20);
        
        lv_obj_set_style_text_color(sub, lv_color_hex(0x666666), 0);
        lv_obj_set_style_text_font(sub, &lv_font_montserrat_12, 0);
        lv_obj_align(sub, LV_ALIGN_CENTER, 0, 10);
        
        lv_timer_handler(); // forca o desenho
        delay(2000); // segura a tela um pouco
    }
}

// ==========================================================================
// Confirmacao de vinculo (overlay sobre qualquer tela)
// ==========================================================================
static lv_obj_t * pair_overlay = NULL;
static lv_obj_t * pair_msg = NULL;
static lv_obj_t * pair_btn_row = NULL;
static lv_obj_t * pair_countdown = NULL;
static lv_timer_t * pair_timer = NULL;
static int pair_seconds_left = 0;

static void pair_close_cb(lv_timer_t * t) {
  ui_hide_pair_confirm();
}

static void pair_countdown_cb(lv_timer_t * t) {
  if (!pair_countdown) return;
  if (pair_seconds_left > 0) pair_seconds_left--;
  lv_label_set_text_fmt(pair_countdown, "Expira em %d:%02d", pair_seconds_left / 60, pair_seconds_left % 60);
}

static void pair_answer(bool accept) {
  if (pair_btn_row) lv_obj_add_flag(pair_btn_row, LV_OBJ_FLAG_HIDDEN);
  if (pair_countdown) lv_obj_add_flag(pair_countdown, LV_OBJ_FLAG_HIDDEN);
  if (pair_msg) lv_label_set_text(pair_msg, "Enviando...");
  if (pair_timer) { lv_timer_del(pair_timer); pair_timer = NULL; }
  cloud_pairing_respond(accept);
}
static void pair_accept_cb(lv_event_t * e) { pair_answer(true); }
static void pair_reject_cb(lv_event_t * e) { pair_answer(false); }

void ui_hide_pair_confirm() {
  if (pair_timer) { lv_timer_del(pair_timer); pair_timer = NULL; }
  if (pair_overlay) { lv_obj_del(pair_overlay); }
  pair_overlay = NULL; pair_msg = NULL; pair_btn_row = NULL; pair_countdown = NULL;
}

void ui_show_pair_confirm(const char* email, const char* deviceName, int expiresIn) {
  ui_hide_pair_confirm();
  pair_seconds_left = expiresIn > 0 ? expiresIn : 120;

  pair_overlay = lv_obj_create(lv_layer_top());
  lv_obj_set_size(pair_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(pair_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(pair_overlay, LV_OPA_80, 0);
  lv_obj_set_style_border_width(pair_overlay, 0, 0);
  lv_obj_set_style_radius(pair_overlay, 0, 0);
  lv_obj_clear_flag(pair_overlay, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(pair_overlay, LV_OBJ_FLAG_CLICKABLE); // bloqueia toques na tela de baixo

  lv_obj_t * card = lv_obj_create(pair_overlay);
  lv_obj_set_size(card, 380, 240);
  lv_obj_center(card);
  lv_obj_set_style_bg_color(card, lv_color_hex(0x111111), 0);
  lv_obj_set_style_border_color(card, lv_color_hex(0x333333), 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_radius(card, 16, 0);
  lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t * title = lv_label_create(card);
  lv_label_set_text(title, "Vincular a uma conta?");
  lv_obj_set_style_text_font(title, &lv_font_montserrat_24, 0);
  lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

  pair_msg = lv_label_create(card);
  lv_label_set_long_mode(pair_msg, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(pair_msg, 340);
  lv_obj_set_style_text_align(pair_msg, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_text_font(pair_msg, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(pair_msg, lv_color_hex(0xBBBBBB), 0);
  if (deviceName && deviceName[0]) {
    lv_label_set_text_fmt(pair_msg, "A conta %s quer adicionar este aparelho como \"%s\".\nSo confirme se foi voce.", email, deviceName);
  } else {
    lv_label_set_text_fmt(pair_msg, "A conta %s quer adicionar este aparelho.\nSo confirme se foi voce.", email);
  }
  lv_obj_align(pair_msg, LV_ALIGN_TOP_MID, 0, 44);

  pair_countdown = lv_label_create(card);
  lv_obj_set_style_text_font(pair_countdown, &lv_font_montserrat_12, 0);
  lv_obj_set_style_text_color(pair_countdown, lv_color_hex(0x666666), 0);
  lv_label_set_text_fmt(pair_countdown, "Expira em %d:%02d", pair_seconds_left / 60, pair_seconds_left % 60);
  lv_obj_align(pair_countdown, LV_ALIGN_BOTTOM_MID, 0, -58);

  pair_btn_row = lv_obj_create(card);
  lv_obj_set_size(pair_btn_row, 340, 50);
  lv_obj_align(pair_btn_row, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_set_style_bg_opa(pair_btn_row, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pair_btn_row, 0, 0);
  lv_obj_set_style_pad_all(pair_btn_row, 0, 0);
  lv_obj_clear_flag(pair_btn_row, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t * btn_no = lv_btn_create(pair_btn_row);
  lv_obj_set_size(btn_no, 160, 46);
  lv_obj_align(btn_no, LV_ALIGN_LEFT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_no, lv_color_hex(0x262626), 0);
  lv_obj_set_style_radius(btn_no, 23, 0);
  lv_obj_add_event_cb(btn_no, pair_reject_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t * lbl_no = lv_label_create(btn_no);
  lv_label_set_text(lbl_no, "RECUSAR");
  lv_obj_set_style_text_font(lbl_no, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_no, lv_color_hex(0xFFFFFF), 0);
  lv_obj_center(lbl_no);

  lv_obj_t * btn_yes = lv_btn_create(pair_btn_row);
  lv_obj_set_size(btn_yes, 160, 46);
  lv_obj_align(btn_yes, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_set_style_bg_color(btn_yes, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_radius(btn_yes, 23, 0);
  lv_obj_add_event_cb(btn_yes, pair_accept_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t * lbl_yes = lv_label_create(btn_yes);
  lv_label_set_text(lbl_yes, "CONFIRMAR");
  lv_obj_set_style_text_font(lbl_yes, &lv_font_montserrat_14, 0);
  lv_obj_set_style_text_color(lbl_yes, lv_color_hex(0x000000), 0);
  lv_obj_center(lbl_yes);

  pair_timer = lv_timer_create(pair_countdown_cb, 1000, NULL);
}

void ui_pair_result(const char* status, bool requestOk) {
  if (!pair_overlay || !pair_msg) return;
  const char* text;
  if (!requestOk) text = "Sem conexao com o servidor. Tente vincular de novo pelo app.";
  else if (strcmp(status, "confirmed") == 0) text = "Aparelho vinculado com sucesso!";
  else if (strcmp(status, "rejected") == 0) text = "Pedido recusado.";
  else if (strcmp(status, "conflict") == 0) text = "Este aparelho ja esta vinculado a uma conta.";
  else if (strcmp(status, "limit") == 0) text = "A conta atingiu o limite de aparelhos.";
  else text = "O pedido expirou. Tente de novo pelo app.";
  lv_label_set_text(pair_msg, text);
  lv_timer_t * t = lv_timer_create(pair_close_cb, 3000, NULL);
  lv_timer_set_repeat_count(t, 1);
}

#endif // BREWW_HAS_LVGL
