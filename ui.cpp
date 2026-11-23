// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Breww
// Telas das placas com tela grande (LVGL 8): leitura, controles, choppeira, sensores, aparelho,
// assistente de Wi-Fi/vinculo, atualizacao e dialogos. Direcao aprovada no prototipo
// (.impeccable/prototype): fundo preto, o numero grande primeiro, blocos #181C25, um verde #71FC8F,
// cinza #6B6F78 secundario, vermelho so para alerta; fonte Outfit.
// Um codigo para os dois tamanhos: as medidas sao as do 3,5" (480x320) e S() multiplica por 1,875 no 7"
// (1024x600). No 3,5" os controles sobem de baixo; no 7" entram pela direita.
// O display e o touch ficam em ui_display.cpp.
#include "config.h"
#if BREWW_HAS_LVGL // tela grande; sem ela, ui_headless.cpp
#include <lvgl.h>
#include <WiFi.h>
#include "ui.h"
#include "ui_display.h"
#include "globals.h"
#include "net_manager.h"
#include "cloud_pairing.h"
#include "ispindel.h"
#include "hal_sensors.h"
#include "hal_profiles.h"
#include "device_settings.h"
#include "fermentation.h"
#include "wifi_setup.h"
#include "version.h"
extern unsigned long lastCompressorOffTime; // hal_relays.cpp

// ---------------------------------------------------------------------------------------------
// Escala, fontes e cores
// ---------------------------------------------------------------------------------------------
#if defined(BREWW_PANEL_DSI)
#define UI_K 1.875f
#define SIDE_SHEET 1 // controles entram pela direita
#define FONT(r35, r7) (&r7)
#else
#define UI_K 1.0f
#define SIDE_SHEET 0 // controles sobem de baixo
#define FONT(r35, r7) (&r35)
#endif
static inline lv_coord_t S(float v) { return (lv_coord_t)(v * UI_K + 0.5f); }
static const lv_coord_t W = SCREEN_WIDTH, H = SCREEN_HEIGHT;
#define SHEET_W S(270) // largura dos controles no 7" (cabem os tres modos)

LV_FONT_DECLARE(outfit35_r12) LV_FONT_DECLARE(outfit35_r13) LV_FONT_DECLARE(outfit35_r15) LV_FONT_DECLARE(outfit35_r18)
LV_FONT_DECLARE(outfit35_r22) LV_FONT_DECLARE(outfit35_r28) LV_FONT_DECLARE(outfit35_r46) LV_FONT_DECLARE(outfit35_m15)
LV_FONT_DECLARE(outfit35_l24) LV_FONT_DECLARE(outfit35_l34) LV_FONT_DECLARE(outfit35_l56) LV_FONT_DECLARE(outfit35_l146)
LV_FONT_DECLARE(outfit35_c40) LV_FONT_DECLARE(outfit35_l88)
LV_FONT_DECLARE(outfit7_r22) LV_FONT_DECLARE(outfit7_r24) LV_FONT_DECLARE(outfit7_r28) LV_FONT_DECLARE(outfit7_r34)
LV_FONT_DECLARE(outfit7_r41) LV_FONT_DECLARE(outfit7_r52) LV_FONT_DECLARE(outfit7_r86) LV_FONT_DECLARE(outfit7_m28)
LV_FONT_DECLARE(outfit7_l45) LV_FONT_DECLARE(outfit7_l64) LV_FONT_DECLARE(outfit7_l105) LV_FONT_DECLARE(outfit7_l274)
LV_FONT_DECLARE(outfit7_c75) LV_FONT_DECLARE(outfit7_l165)
#define F_R12  FONT(outfit35_r12, outfit7_r22)
#define F_R13  FONT(outfit35_r13, outfit7_r24)
#define F_R15  FONT(outfit35_r15, outfit7_r28)
#define F_R18  FONT(outfit35_r18, outfit7_r34)   // tem os icones do teclado
#define F_R22  FONT(outfit35_r22, outfit7_r41)
#define F_R28  FONT(outfit35_r28, outfit7_r52)
#define F_R46  FONT(outfit35_r46, outfit7_r86)
#define F_M15  FONT(outfit35_m15, outfit7_m28)
#define F_L24  FONT(outfit35_l24, outfit7_l45)   // so algarismos , . - ° %
#define F_L34  FONT(outfit35_l34, outfit7_l64)
#define F_L56  FONT(outfit35_l56, outfit7_l105)
#define F_L88  FONT(outfit35_l88, outfit7_l165)   // decimal do numero grande
#define F_L146 FONT(outfit35_l146, outfit7_l274)
#define F_C40  FONT(outfit35_c40, outfit7_c75)   // so A-Z 0-9 -

#if defined(BREWW_PANEL_DSI)
LV_IMG_DECLARE(img_wm7_top) LV_IMG_DECLARE(img_wm7_splash)
#define IMG_WM_TOP (&img_wm7_top)
#define IMG_WM_SPLASH (&img_wm7_splash)
#else
LV_IMG_DECLARE(img_wm35_top) LV_IMG_DECLARE(img_wm35_splash)
#define IMG_WM_TOP (&img_wm35_top)
#define IMG_WM_SPLASH (&img_wm35_splash)
#endif

#define C_GROUND 0x000000
#define C_BLOCK  0x181C25
#define C_BLOCK2 0x202532
#define C_SHEET  0x0B0D12
#define C_LINE   0x262A33   // linha de 8% de branco sobre o bloco
#define C_INK    0xECECF1
#define C_INK2   0x898E99
#define C_GRAY   0x6B6F78
#define C_GREEN  0x71FC8F
#define C_ONGRN  0x0B0C10
#define C_RED    0xFF6B6B
#define C_AMBER  0xF5C76B
#define C_COOL   0x7CC7FF
#define C_ALERT_LINE 0x5A2E33 // borda de bloco/pilula em alerta

#define SYM_BACKSPACE "\xEF\x95\x9A"
#define SYM_SHIFT     "\xEF\x81\xA2"
#define SYM_WIFI      "\xEF\x87\xAB" // na fonte do topo (F_R13)
#define SYM_CABLE     "\xEF\x9B\xBF" // network-wired

// ---------------------------------------------------------------------------------------------
// Peças de montagem
// ---------------------------------------------------------------------------------------------
static lv_obj_t *box(lv_obj_t *parent) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_clear_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
  return o;
}
static lv_obj_t *label(lv_obj_t *parent, const lv_font_t *f, uint32_t color, const char *text = "") {
  lv_obj_t *l = lv_label_create(parent);
  lv_obj_set_style_text_font(l, f, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  lv_label_set_text(l, text);
  return l;
}
static void fill_block(lv_obj_t *o, lv_coord_t radius) {
  lv_obj_set_style_bg_color(o, lv_color_hex(C_BLOCK), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_border_width(o, 1, 0);
  lv_obj_set_style_radius(o, radius, 0);
}
static lv_obj_t *flex(lv_obj_t *parent, lv_flex_flow_t flow, lv_coord_t gap) {
  lv_obj_t *o = box(parent);
  lv_obj_set_flex_flow(o, flow);
  lv_obj_set_style_pad_column(o, gap, 0);
  lv_obj_set_style_pad_row(o, gap, 0);
  return o;
}
static void dot_set(lv_obj_t *o, uint32_t color, bool ring) {
  const lv_color_t v = lv_color_hex(color);
  if (lv_obj_get_style_bg_color(o, 0).full == v.full && (lv_obj_get_style_border_width(o, 0) > 0) == ring) return;
  lv_obj_set_style_bg_color(o, v, 0);
  lv_obj_set_style_bg_opa(o, ring ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(o, lv_color_hex(color), 0);
  lv_obj_set_style_border_width(o, ring ? S(2) : 0, 0);
}
static lv_obj_t *dot(lv_obj_t *parent, lv_coord_t d) {
  lv_obj_t *o = box(parent);
  lv_obj_set_size(o, d, d);
  lv_obj_set_style_radius(o, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(o, lv_color_hex(C_GRAY), 0);
  return o;
}

enum BtnKind { BTN_SOFT, BTN_PRIMARY, BTN_OUTLINE, BTN_GHOST };
static lv_obj_t *button(lv_obj_t *parent, const char *text, BtnKind kind, lv_event_cb_t cb, void *ud = NULL,
                        const lv_font_t *f = NULL) {
  lv_obj_t *b = lv_btn_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_min_height(b, S(48), 0);
  lv_obj_set_style_pad_hor(b, S(14), 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  uint32_t ink = C_INK;
  switch (kind) {
    case BTN_PRIMARY: lv_obj_set_style_bg_color(b, lv_color_hex(C_GREEN), 0); ink = C_ONGRN;
                      lv_obj_set_style_bg_color(b, lv_color_hex(0x9BFDB0), LV_STATE_PRESSED); break;
    case BTN_SOFT:    lv_obj_set_style_bg_color(b, lv_color_white(), 0); lv_obj_set_style_bg_opa(b, 18, 0);
                      lv_obj_set_style_bg_opa(b, 42, LV_STATE_PRESSED); break;
    case BTN_OUTLINE: lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0); lv_obj_set_style_border_width(b, 1, 0);
                      lv_obj_set_style_border_color(b, lv_color_hex(C_LINE), 0); ink = C_INK2;
                      lv_obj_set_style_bg_color(b, lv_color_white(), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(b, 30, LV_STATE_PRESSED); break;
    case BTN_GHOST:   lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0); ink = C_INK2;
                      lv_obj_set_style_bg_color(b, lv_color_white(), LV_STATE_PRESSED); lv_obj_set_style_bg_opa(b, 30, LV_STATE_PRESSED); break;
  }
  lv_obj_set_style_opa(b, LV_OPA_50, LV_STATE_DISABLED);
  lv_obj_t *l = label(b, f ? f : (kind == BTN_PRIMARY ? F_M15 : F_R15), ink, text);
  lv_obj_center(l);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, ud);
  return b;
}
static lv_obj_t *btn_label(lv_obj_t *b) { return lv_obj_get_child(b, 0); }

// --- Atualizacao barata: so toca no objeto quando o valor muda (no LVGL todo set redesenha) ---
static void set_text(lv_obj_t *l, const char *t) {
  uint32_t h = 2166136261u; // FNV-1a do texto, guardado no proprio rotulo
  for (const char *p = t; *p; p++) h = (h ^ (uint8_t)*p) * 16777619u;
  if ((uint32_t)(uintptr_t)lv_obj_get_user_data(l) == h) return;
  lv_obj_set_user_data(l, (void *)(uintptr_t)h);
  lv_label_set_text(l, t);
}
static void set_text(lv_obj_t *l, const String &t) { set_text(l, t.c_str()); }
static void set_hidden(lv_obj_t *o, bool h) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) == h) return;
  if (h) lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(o, LV_OBJ_FLAG_HIDDEN);
}
static void set_text_color(lv_obj_t *o, uint32_t c) {
  const lv_color_t v = lv_color_hex(c);
  if (lv_obj_get_style_text_color(o, 0).full != v.full) lv_obj_set_style_text_color(o, v, 0);
}
static void set_border_color(lv_obj_t *o, uint32_t c) {
  const lv_color_t v = lv_color_hex(c);
  if (lv_obj_get_style_border_color(o, 0).full != v.full) lv_obj_set_style_border_color(o, v, 0);
}
static void set_font(lv_obj_t *o, const lv_font_t *f) { if (lv_obj_get_style_text_font(o, 0) != f) lv_obj_set_style_text_font(o, f, 0); }
static void set_grow(lv_obj_t *o, uint8_t g) { if (lv_obj_get_style_flex_grow(o, 0) != g) lv_obj_set_flex_grow(o, g); }

static String fmt1(float v) { // 26,1 (virgula, como no app)
  char b[16];
  snprintf(b, sizeof(b), "%.1f", v);
  for (char *p = b; *p; p++) if (*p == '.') *p = ',';
  return String(b);
}
static String dur_left(long sec) {
  if (sec < 0) sec = 0;
  const long d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60;
  char b[24];
  if (d) snprintf(b, sizeof(b), "%ldd %ldh", d, h);
  else if (h) snprintf(b, sizeof(b), "%ldh %02ldmin", h, m);
  else snprintf(b, sizeof(b), "%ld min", m);
  return String(b);
}

// ---------------------------------------------------------------------------------------------
// Estado do aparelho, em palavras
// ---------------------------------------------------------------------------------------------
static const char *MODE_NAMES[] = {"Fermentador", "Geladeira", "Choppeira", "Limpeza"};
static const char *mode_name(int m) { return (m >= 0 && m <= 3) ? MODE_NAMES[m] : "Breww"; }

// Alvo ajustado na tela: aparece na hora e e gravado/publicado 0,8 s depois do ultimo toque
static float pendingTarget = NAN;
static unsigned long pendingAt = 0;
static float shown_target() { return isnan(pendingTarget) ? currentSetpoint : pendingTarget; }

struct RelayState { uint32_t color; const char *word; bool alert; bool ring; };
static RelayState relay_state() {
  if (!control_sensor_ok()) return {C_RED, "Sensor sem leitura", true, false};
  // A pausa da etapa so congela o tempo: os reles seguem o alvo, entao o estado mostra o rele de verdade
  if (isCoolerOn) return {C_COOL, "Resfriando", false, false};
  if (isHeaterOn) return {C_AMBER, "Aquecendo", false, false};
  const float hyst = currentMode == MODE_CHOPP ? 0.3f : DEFAULT_HISTERESE;
  if (control_temp() - currentSetpoint > hyst && millis() - lastCompressorOffTime < COMPRESSOR_DELAY_SEC * 1000UL)
    return {C_COOL, "Aguardando compressor", false, true}; // protecao de 5 min depois de desligar
  return {C_GRAY, "Est\xC3\xA1vel", false, false};
}
static bool batch_active() { return currentMode == MODE_FERM && isRampActive && numRampSteps > 0; }
static long step_left_sec() {
  if (!batch_active()) return 0;
  return (long)(rampSteps[currentRampStep].durationDays * 86400.0f - ramp_step_elapsed_hours() * 3600.0f);
}
static float step_progress() {
  if (!batch_active()) return 0;
  const float total = rampSteps[currentRampStep].durationDays * 24.0f;
  return total > 0 ? constrain(ramp_step_elapsed_hours() / total, 0.0f, 1.0f) : 0;
}
static bool ispindel_ok() { return ispindelGravity > 0; } // ultimo valor do densimetro (salvo), como no app
static float gravity_sg() { return ispindelGravity > 500 ? ispindelGravity / 1000.0f : ispindelGravity; }
static bool key_rejected() { return strncmp(cloud_status_text(), "chave recusada", 14) == 0; }

// ---------------------------------------------------------------------------------------------
// Toast e dialogo (camada de cima, sobre qualquer tela)
// ---------------------------------------------------------------------------------------------
static void toast(const char *msg) {
  lv_obj_t *t = box(lv_layer_top());
  fill_block(t, LV_RADIUS_CIRCLE);
  lv_obj_set_style_bg_color(t, lv_color_hex(C_BLOCK2), 0);
  lv_obj_set_style_pad_hor(t, S(16), 0);
  lv_obj_set_style_pad_ver(t, S(8), 0);
  lv_obj_set_size(t, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  label(t, F_R13, C_INK, msg);
  lv_obj_align(t, LV_ALIGN_TOP_MID, 0, S(44));
  lv_timer_t *tm = lv_timer_create([](lv_timer_t *x) { lv_obj_del((lv_obj_t *)x->user_data); }, 1800, t);
  lv_timer_set_repeat_count(tm, 1);
}

typedef void (*Action)();
static lv_obj_t *dlgRoot = NULL;
static Action dlgOk = NULL, dlgNo = NULL;
static bool dlgIsPair = false; // o dialogo aberto e o pedido de vinculo do app
static void dlg_close() { if (dlgRoot) { lv_obj_del_async(dlgRoot); dlgRoot = NULL; } dlgIsPair = false; }
static void dlg_btn_cb(lv_event_t *e) {
  const bool ok = (bool)(uintptr_t)lv_event_get_user_data(e);
  Action a = ok ? dlgOk : dlgNo;
  dlg_close();
  if (a) a();
}
static lv_obj_t *dlgText = NULL, *dlgActs = NULL;
static void dialog(const char *title, const char *text, const char *okText, Action onOk, bool danger = false,
                   const char *noText = "Cancelar", Action onNo = NULL) {
  if (dlgIsPair) return; // o pedido de vinculo precisa de resposta antes de outro dialogo
  dlg_close();
  dlgOk = onOk; dlgNo = onNo;
  dlgRoot = box(lv_layer_top());
  lv_obj_add_flag(dlgRoot, LV_OBJ_FLAG_CLICKABLE); // nao deixa o toque passar para a tela de baixo
  lv_obj_set_size(dlgRoot, W, H);
  lv_obj_set_style_bg_color(dlgRoot, lv_color_hex(0x020407), 0);
  lv_obj_set_style_bg_opa(dlgRoot, 184, 0);
  lv_obj_t *b = flex(dlgRoot, LV_FLEX_FLOW_COLUMN, S(8));
  fill_block(b, S(24));
  lv_obj_set_style_pad_all(b, S(18), 0);
  lv_obj_set_size(b, S(380), LV_SIZE_CONTENT);
  lv_obj_center(b);
  lv_obj_t *t = label(b, F_R22, C_INK, title);
  lv_obj_set_width(t, lv_pct(100));
  lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
  dlgText = label(b, F_R15, C_INK2, text);
  lv_obj_set_width(dlgText, lv_pct(100));
  lv_label_set_long_mode(dlgText, LV_LABEL_LONG_WRAP);
  dlgActs = flex(b, LV_FLEX_FLOW_ROW, S(8));
  lv_obj_set_size(dlgActs, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(dlgActs, S(6), 0);
  lv_obj_t *no = button(dlgActs, noText, BTN_SOFT, dlg_btn_cb, (void *)0);
  lv_obj_set_flex_grow(no, 1);
  lv_obj_t *ok = button(dlgActs, okText, danger ? BTN_SOFT : BTN_PRIMARY, dlg_btn_cb, (void *)1);
  if (danger) lv_obj_set_style_text_color(btn_label(ok), lv_color_hex(C_RED), 0);
  lv_obj_set_flex_grow(ok, 1);
}

// ---------------------------------------------------------------------------------------------
// Troca de tela: a de leitura fica viva; as outras sao apagadas ao sair
// ---------------------------------------------------------------------------------------------
static lv_obj_t *scr_home = NULL;
static bool homeDirty = true;
static void build_home();
static void show(lv_obj_t *scr) {
  lv_obj_t *cur = lv_scr_act();
  lv_scr_load(scr);
  // A anterior (menos a de leitura) sai depois do toque atual: apagar dentro do proprio evento trava
  if (cur && cur != scr_home && cur != scr) lv_obj_del_async(cur);
}
static lv_obj_t *new_screen() {
  lv_obj_t *s = lv_obj_create(NULL);
  lv_obj_remove_style_all(s);
  lv_obj_set_style_bg_color(s, lv_color_hex(C_GROUND), 0);
  lv_obj_set_style_bg_opa(s, LV_OPA_COVER, 0);
  lv_obj_clear_flag(s, LV_OBJ_FLAG_SCROLLABLE);
  return s;
}
static void go_home() {
  if (homeDirty || !scr_home) {
    lv_obj_t *old = scr_home;
    build_home();
    if (old && old != lv_scr_act()) lv_obj_del_async(old);
  }
  show(scr_home);
}
static void go_home_cb(lv_event_t *) { go_home(); }

// Pagina comum: cabecalho (voltar + titulo [+ acao]) e corpo rolavel
struct Page { lv_obj_t *scr, *hd, *body; };
static Page page(const char *title, lv_event_cb_t backCb) {
  Page p;
  p.scr = new_screen();
  lv_obj_t *col = flex(p.scr, LV_FLEX_FLOW_COLUMN, S(8));
  lv_obj_set_size(col, W, H);
  lv_obj_set_style_pad_hor(col, S(20), 0);
  lv_obj_set_style_pad_ver(col, S(14), 0);
  p.hd = flex(col, LV_FLEX_FLOW_ROW, S(8));
  lv_obj_set_size(p.hd, lv_pct(100), S(44));
  lv_obj_set_flex_align(p.hd, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  if (backCb) {
    lv_obj_t *bk = button(p.hd, "\xE2\x80\xB9", BTN_OUTLINE, backCb, NULL, F_R22);
    lv_obj_set_size(bk, S(48), S(44));
    lv_obj_set_style_pad_hor(bk, 0, 0);
    lv_obj_set_style_text_color(btn_label(bk), lv_color_hex(C_INK), 0);
  }
  lv_obj_t *t = label(p.hd, F_R22, C_INK, title);
  lv_obj_set_flex_grow(t, 1);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  p.body = flex(col, LV_FLEX_FLOW_COLUMN, S(8));
  lv_obj_set_width(p.body, lv_pct(100));
  lv_obj_set_flex_grow(p.body, 1);
  lv_obj_add_flag(p.body, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE)); // o LVGL so rola o que aceita toque
  lv_obj_set_scroll_dir(p.body, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(p.body, LV_SCROLLBAR_MODE_OFF);
  return p;
}

// Bloco de "nome + valor" (tile)
struct Tile { lv_obj_t *o, *lb, *row, *dot, *v, *sub, *bar; };
static Tile tile(lv_obj_t *parent, lv_coord_t h) {
  Tile t;
  t.o = flex(parent, LV_FLEX_FLOW_COLUMN, S(1));
  fill_block(t.o, S(18));
  lv_obj_set_height(t.o, h);
  lv_obj_set_style_pad_hor(t.o, S(14), 0);
  lv_obj_set_flex_align(t.o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  t.lb = label(t.o, F_R12, C_INK2);
  lv_obj_set_width(t.lb, lv_pct(100));
  lv_label_set_long_mode(t.lb, LV_LABEL_LONG_DOT);
  t.row = flex(t.o, LV_FLEX_FLOW_ROW, S(6));
  lv_obj_set_size(t.row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(t.row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  t.dot = dot(t.row, S(9));
  lv_obj_add_flag(t.dot, LV_OBJ_FLAG_HIDDEN);
  t.v = label(t.row, F_L24, C_INK);
  lv_obj_set_flex_grow(t.v, 1);
  lv_label_set_long_mode(t.v, LV_LABEL_LONG_DOT);
  t.sub = label(t.o, F_R12, C_GRAY);
  lv_obj_add_flag(t.sub, LV_OBJ_FLAG_HIDDEN);
  t.bar = box(t.o);
  lv_obj_set_size(t.bar, lv_pct(100), S(3));
  lv_obj_set_style_radius(t.bar, S(2), 0);
  lv_obj_set_style_bg_color(t.bar, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(t.bar, 26, 0);
  lv_obj_set_style_pad_top(t.bar, 0, 0);
  lv_obj_t *fill = box(t.bar);
  lv_obj_set_height(fill, lv_pct(100));
  lv_obj_set_style_radius(fill, S(2), 0);
  lv_obj_set_style_bg_color(fill, lv_color_hex(C_GREEN), 0);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
  lv_obj_add_flag(t.bar, LV_OBJ_FLAG_HIDDEN);
  return t;
}
// numeric: fonte fina de algarismos; senao, texto
static void tile_set(Tile &t, const char *lb, const String &v, bool numeric, uint32_t color = C_INK, bool alert = false) {
  set_text(t.lb, lb);
  set_font(t.v, numeric ? F_L24 : F_R15);
  set_text_color(t.v, color);
  set_text(t.v, v);
  set_border_color(t.o, alert ? C_ALERT_LINE : C_LINE);
}
static void tile_bar(Tile &t, float frac) {
  set_hidden(t.bar, frac < 0);
  if (frac < 0) return;
  lv_obj_t *fill = lv_obj_get_child(t.bar, 0);
  const lv_coord_t w = lv_pct((int)(frac * 100));
  if (lv_obj_get_style_width(fill, 0) != w) lv_obj_set_width(fill, w);
}

// ---------------------------------------------------------------------------------------------
// Tela de leitura (inicio) + folha de controles
// ---------------------------------------------------------------------------------------------
static struct Home {
  lv_obj_t *logo, *mini, *right, *warn, *topl, *clock, *net;
  lv_obj_t *hero, *state;             // fermentador/geladeira
  lv_obj_t *hInt, *hDec, *hUnit;
  lv_obj_t *stDot, *stWord;
  lv_obj_t *menu, *beer, *style, *abv, *ibu; // choppeira
  lv_obj_t *tiles;
  Tile t[4];
  lv_obj_t *sheet, *tgtVal, *tgtMinus, *tgtPlus, *stepName, *stepLeft, *pauseLbl, *modeBtn[3];
  bool open;
  int builtMode;
  int sheetSig;
} hm;

static void sheet_set_open(bool open);
static void build_sheet();

static void home_click_cb(lv_event_t *e) {
  if (lv_event_get_target(e) != scr_home) return;
  sheet_set_open(!hm.open);
}

static void anim_y(void *o, int32_t v) { lv_obj_set_y((lv_obj_t *)o, v); }
static void anim_x(void *o, int32_t v) { lv_obj_set_x((lv_obj_t *)o, v); }
static void anim_tx(void *o, int32_t v) { lv_obj_set_style_translate_x((lv_obj_t *)o, v, 0); }
static void animate(lv_obj_t *o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms = 200) {
  if (!o) return;
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, cb);
  lv_anim_set_values(&a, from, to);
  lv_anim_set_time(&a, ms);
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_start(&a);
}

static void sheet_set_open(bool open) {
  if (!hm.sheet || open == hm.open) return;
  hm.open = open;
  const int32_t heroOpa = open ? LV_OPA_TRANSP : LV_OPA_COVER;
#if SIDE_SHEET
  const lv_coord_t sw = SHEET_W;
  animate(hm.sheet, anim_x, open ? W : W - sw, open ? W - sw : W);
  // A leitura continua visivel, empurrada para a esquerda; os blocos de baixo saem
  if (hm.hero) { animate(hm.hero, anim_tx, open ? 0 : -S(138), open ? -S(138) : 0); animate(hm.state, anim_tx, open ? 0 : -S(138), open ? -S(138) : 0); }
  if (hm.menu) lv_obj_set_width(hm.menu, open ? W - S(48) - sw : W - S(48));
  lv_obj_set_width(hm.right, open ? W - sw - S(40) - lv_obj_get_width(hm.logo) - S(20) : W - S(40) - lv_obj_get_width(hm.logo) - S(20));
  lv_obj_align(hm.right, LV_ALIGN_TOP_RIGHT, -S(20) - (open ? sw : 0), S(12));
  set_hidden(hm.tiles, open);
  (void)heroOpa;
#else
  const lv_coord_t sh = S(262);
  animate(hm.sheet, anim_y, open ? H : H - sh, open ? H - sh : H);
  // No 3,5" a leitura vira uma linha no topo enquanto os controles estao abertos
  lv_obj_t *hide[] = {hm.hero, hm.state, hm.menu, hm.tiles, hm.logo};
  for (lv_obj_t *o : hide) if (o) set_hidden(o, open);
  (void)heroOpa;
  if (open) { lv_obj_clear_flag(hm.mini, LV_OBJ_FLAG_HIDDEN); lv_obj_add_flag(hm.topl, LV_OBJ_FLAG_HIDDEN); }
  else { lv_obj_add_flag(hm.mini, LV_OBJ_FLAG_HIDDEN); lv_obj_clear_flag(hm.topl, LV_OBJ_FLAG_HIDDEN); }
#endif
}

// --- acoes da folha ---
static void target_step(float d) {
  float v = shown_target() + d;
  v = roundf(constrain(v, -5.0f, 40.0f) * 10.0f) / 10.0f;
  pendingTarget = v;
  pendingAt = millis();
}
static void target_commit() {
  if (isnan(pendingTarget) || millis() - pendingAt < 800) return;
  const float v = pendingTarget;
  pendingTarget = NAN;
  if (batch_active()) { rampSteps[currentRampStep].targetTemp = v; save_ramp_steps(); } // alvo da etapa atual
  settings_set_setpoint(v); // publica na hora; grava com debounce
}
static void tgt_cb(lv_event_t *e) {
  const lv_event_code_t c = lv_event_get_code(e);
  if (c != LV_EVENT_PRESSED && c != LV_EVENT_LONG_PRESSED_REPEAT) return;
  target_step((float)(intptr_t)lv_event_get_user_data(e) / 10.0f);
  if (hm.tgtVal) set_text(hm.tgtVal, fmt1(shown_target()) + "\xC2\xB0");
}
static int pendingMode = -1;
static void mode_apply() {
  if (pendingMode < 0) return;
  settings_set_mode(pendingMode);
  char b[40];
  snprintf(b, sizeof(b), "Modo %s", mode_name(pendingMode));
  pendingMode = -1;
  homeDirty = true;
  lv_obj_t *old = scr_home;
  build_home();
  lv_scr_load(scr_home);
  if (old) lv_obj_del_async(old);
  toast(b);
}
static void mode_cb(lv_event_t *e) {
  const int m = (int)(intptr_t)lv_event_get_user_data(e);
  if (m == currentMode) return;
  pendingMode = m;
  static char title[48], text[120];
  snprintf(title, sizeof(title), "Trocar para %s?", mode_name(m));
  if (m == MODE_FERM) snprintf(text, sizeof(text), "%s", "O controle segue o perfil do lote. Sem lote ativo, fica no alvo atual at\xC3\xA9 um lote come\xC3\xA7""ar no app.");
  else if (m == MODE_GELADEIRA) snprintf(text, sizeof(text), "%s", "O controle segura um alvo fixo.");
  else snprintf(text, sizeof(text), "%s", "O controle segura o alvo da choppeira e a tela mostra a cerveja na torneira.");
  dialog(title, text, "Trocar", mode_apply, false, "Cancelar", [] { pendingMode = -1; });
}
static void pause_cb(lv_event_t *) {
  if (!ramp_toggle_pause()) return;
  toast(isRampPaused ? "Etapa pausada" : "Etapa retomada");
}
static void adv_apply() {
  if (currentRampStep >= numRampSteps - 1) { ferm_stop_batch(); toast("Lote encerrado"); }
  else if (ramp_skip_step()) toast("Etapa avan\xC3\xA7""ada");
  homeDirty = true;
}
static void adv_cb(lv_event_t *) {
  if (!batch_active()) return;
  static char title[64], text[140];
  if (currentRampStep >= numRampSteps - 1) {
    snprintf(title, sizeof(title), "Encerrar o lote?");
    snprintf(text, sizeof(text), "%s", "\xC3\x89 a \xC3\xBAltima etapa. O perfil termina e o Fermentador fica no alvo atual.");
    dialog(title, text, "Encerrar", adv_apply, true);
  } else {
    const RampStep &n = rampSteps[currentRampStep + 1];
    snprintf(title, sizeof(title), "Avan\xC3\xA7""ar para %s?", n.name);
    snprintf(text, sizeof(text), "O alvo muda para %s\xC2\xB0 agora. A etapa atual termina antes do previsto.", fmt1(n.targetTemp).c_str());
    dialog(title, text, "Avan\xC3\xA7""ar", adv_apply);
  }
}
static void open_sensors();
static void open_device();
static void open_beer();
static void sensors_cb(lv_event_t *) { open_sensors(); }
static void device_cb(lv_event_t *) { open_device(); }
static void beer_cb(lv_event_t *) { open_beer(); }

static int sheet_signature() { return currentMode * 10 + (batch_active() ? 1 : 0); }

static void build_sheet() {
  if (hm.sheet) lv_obj_del(hm.sheet);
  hm.sheetSig = sheet_signature();
  hm.stepName = hm.stepLeft = hm.pauseLbl = NULL;
  lv_obj_t *sh = flex(scr_home, LV_FLEX_FLOW_COLUMN, S(8));
  hm.sheet = sh;
  lv_obj_add_flag(sh, LV_OBJ_FLAG_CLICKABLE); // toque na folha nao fecha
  lv_obj_set_style_bg_color(sh, lv_color_hex(C_SHEET), 0);
  lv_obj_set_style_bg_opa(sh, LV_OPA_COVER, 0);
  lv_obj_set_style_border_color(sh, lv_color_hex(C_LINE), 0);
  lv_obj_set_style_border_width(sh, 1, 0);
  lv_obj_set_style_radius(sh, S(26), 0);
#if SIDE_SHEET
  lv_obj_set_style_border_side(sh, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_size(sh, SHEET_W + S(30), H);
  lv_obj_set_pos(sh, hm.open ? W - SHEET_W : W, 0);
  lv_obj_set_style_pad_left(sh, S(12), 0);
  lv_obj_set_style_pad_right(sh, S(12) + S(30), 0);
  lv_obj_set_style_pad_ver(sh, S(16), 0);
  lv_obj_set_style_pad_row(sh, S(10), 0);
#else
  lv_obj_set_style_border_side(sh, LV_BORDER_SIDE_TOP, 0);
  lv_obj_set_size(sh, W, S(262) + S(30));
  lv_obj_set_pos(sh, 0, hm.open ? H - S(262) : H);
  lv_obj_set_style_pad_hor(sh, S(14), 0);
  lv_obj_set_style_pad_top(sh, S(10), 0);
  lv_obj_set_style_pad_bottom(sh, S(12) + S(30), 0);
  lv_obj_set_flex_align(sh, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *grab = box(sh);
  lv_obj_set_size(grab, S(44), S(5));
  lv_obj_set_style_radius(grab, S(3), 0);
  lv_obj_set_style_bg_color(grab, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(grab, 46, 0);
#endif

  // Alvo: [rotulo] [-] valor [+]
  lv_obj_t *tg = flex(sh, LV_FLEX_FLOW_ROW_WRAP, S(8));
  fill_block(tg, S(20));
  lv_obj_set_size(tg, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_all(tg, S(6), 0);
  lv_obj_set_flex_align(tg, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *tl = label(tg, F_R12, C_INK2, batch_active() ? "Alvo da etapa" : "Alvo");
#if SIDE_SHEET
  lv_obj_set_width(tl, lv_pct(100));
  lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_pad_top(tl, S(4), 0);
#else
  lv_obj_set_width(tl, S(60));
  lv_label_set_long_mode(tl, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_pad_left(tl, S(8), 0);
#endif
  hm.tgtMinus = button(tg, "\xE2\x88\x92", BTN_SOFT, NULL, NULL, F_R28);
  hm.tgtVal = label(tg, F_L34, C_INK, "--");
  lv_obj_set_flex_grow(hm.tgtVal, 1);
  lv_obj_set_style_text_align(hm.tgtVal, LV_TEXT_ALIGN_CENTER, 0);
  hm.tgtPlus = button(tg, "+", BTN_SOFT, NULL, NULL, F_R28);
  lv_obj_t *pm[] = {hm.tgtMinus, hm.tgtPlus};
  for (int i = 0; i < 2; i++) {
    lv_obj_set_size(pm[i], SIDE_SHEET ? S(52) : S(60), S(48));
    lv_obj_set_style_pad_hor(pm[i], 0, 0);
    void *d = (void *)(intptr_t)(i ? 1 : -1);
    lv_obj_add_event_cb(pm[i], tgt_cb, LV_EVENT_PRESSED, d);
    lv_obj_add_event_cb(pm[i], tgt_cb, LV_EVENT_LONG_PRESSED_REPEAT, d);
  }

  // Modos
  lv_obj_t *modes = flex(sh, LV_FLEX_FLOW_ROW, S(5));
  lv_obj_set_size(modes, lv_pct(100), LV_SIZE_CONTENT);
  for (int m = 0; m < 3; m++) {
    const bool on = m == currentMode;
    lv_obj_t *b = button(modes, MODE_NAMES[m], on ? BTN_PRIMARY : BTN_OUTLINE, mode_cb, (void *)(intptr_t)m,
                         SIDE_SHEET ? F_R12 : F_R15);
    lv_obj_set_style_min_height(b, SIDE_SHEET ? S(40) : S(48), 0);
    lv_obj_set_style_pad_hor(b, S(4), 0);
    lv_obj_set_flex_grow(b, 1);
    hm.modeBtn[m] = b;
  }

  // Etapa / cerveja / alvo fixo
  lv_obj_t *row = flex(sh, SIDE_SHEET ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW, S(8));
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_t *info = flex(row, LV_FLEX_FLOW_COLUMN, 0);
  lv_obj_set_height(info, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(info, 1);
  if (SIDE_SHEET) lv_obj_set_width(info, lv_pct(100));
  lv_obj_t *l1 = label(info, F_R15, C_INK);
  lv_obj_t *l2 = label(info, F_R12, C_INK2);
  lv_label_set_long_mode(l1, LV_LABEL_LONG_DOT);
  lv_obj_set_width(l1, lv_pct(100));
  if (batch_active()) {
    hm.stepName = l1; hm.stepLeft = l2;
    lv_obj_t *pb = button(row, "Pausar", BTN_SOFT, pause_cb);
    hm.pauseLbl = btn_label(pb);
    lv_obj_t *ab = button(row, "Avan\xC3\xA7""ar", BTN_SOFT, adv_cb);
    if (SIDE_SHEET) { lv_obj_set_flex_grow(pb, 1); lv_obj_set_flex_grow(ab, 1); }
  } else if (currentMode == MODE_CHOPP) {
    lv_label_set_text(l1, beerName.length() ? beerName.c_str() : "Sem nome");
    lv_label_set_text(l2, beerType.c_str());
    lv_obj_t *eb = button(row, "Editar cerveja", BTN_SOFT, beer_cb);
    if (SIDE_SHEET) lv_obj_set_flex_grow(eb, 1);
  } else if (currentMode == MODE_FERM) {
    lv_label_set_text(l1, "Sem lote ativo");
    lv_label_set_text(l2, "Comece um lote pelo app");
  } else {
    lv_label_set_text(l1, "Alvo fixo");
    hm.stepLeft = l2; // umidade
  }

  // Sensores / Aparelho
  lv_obj_t *links = flex(sh, LV_FLEX_FLOW_ROW, S(6));
  lv_obj_set_size(links, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_t *ls = button(links, "Sensores", BTN_OUTLINE, sensors_cb, NULL, F_R13);
  lv_obj_t *ld = button(links, "Aparelho", BTN_OUTLINE, device_cb, NULL, F_R13);
  lv_obj_t *lk[] = {ls, ld};
  for (lv_obj_t *b : lk) { lv_obj_set_flex_grow(b, 1); lv_obj_set_style_min_height(b, SIDE_SHEET ? S(40) : S(44), 0); }
}

static void build_top(lv_obj_t *scr) {
  hm.logo = lv_img_create(scr);
  lv_img_set_src(hm.logo, IMG_WM_TOP);
  lv_obj_set_style_img_recolor(hm.logo, lv_color_hex(C_INK), 0);
  lv_obj_set_style_img_recolor_opa(hm.logo, LV_OPA_COVER, 0);
  lv_obj_set_pos(hm.logo, S(20), S(15));
  hm.mini = flex(scr, LV_FLEX_FLOW_ROW, S(8));
  lv_obj_set_size(hm.mini, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_pos(hm.mini, S(20), S(10));
  lv_obj_set_flex_align(hm.mini, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_flag(hm.mini, LV_OBJ_FLAG_HIDDEN);
  label(hm.mini, F_R18, C_INK);
  dot(hm.mini, S(9));
  label(hm.mini, F_R18, C_INK2);

  hm.right = flex(scr, LV_FLEX_FLOW_ROW, S(14));
  const lv_coord_t rw = W - S(40) - (lv_coord_t)IMG_WM_TOP->header.w - S(20);
  lv_obj_set_size(hm.right, rw, LV_SIZE_CONTENT);
  lv_obj_align(hm.right, LV_ALIGN_TOP_RIGHT, -S(20), S(12));
  lv_obj_set_flex_align(hm.right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  hm.warn = box(hm.right);
  lv_obj_set_size(hm.warn, LV_SIZE_CONTENT, S(24));
  lv_obj_set_style_radius(hm.warn, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(hm.warn, 1, 0);
  lv_obj_set_style_pad_hor(hm.warn, S(10), 0);
  lv_obj_t *wl = label(hm.warn, F_R12, C_AMBER);
  lv_obj_center(wl);
  lv_obj_add_flag(hm.warn, LV_OBJ_FLAG_HIDDEN);
  hm.topl = label(hm.right, F_R13, C_INK2);
  lv_label_set_long_mode(hm.topl, LV_LABEL_LONG_DOT);
  lv_obj_set_style_max_width(hm.topl, rw / 2, 0);
  hm.clock = label(hm.right, F_R13, C_INK2, "--:--");
  hm.net = label(hm.right, F_R13, C_INK2, ""); // Wi-Fi ou cabo, a direita do relogio
  lv_obj_set_style_pad_left(hm.net, -S(6), 0);
}

static void build_home() {
  memset(&hm, 0, sizeof(hm));
  hm.builtMode = currentMode;
  homeDirty = false;
  request_dashboard_refresh = false;
  scr_home = new_screen();
  lv_obj_add_flag(scr_home, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(scr_home, home_click_cb, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(scr_home, [](lv_event_t *e) { if (lv_event_get_target(e) == scr_home) { scr_home = NULL; memset(&hm, 0, sizeof(hm)); homeDirty = true; } }, LV_EVENT_DELETE, NULL);
  build_top(scr_home);

  if (currentMode == MODE_CHOPP) {
    // Cardapio: nome, estilo e alcool/amargor centralizados
    hm.menu = flex(scr_home, LV_FLEX_FLOW_COLUMN, S(4));
    lv_obj_set_size(hm.menu, W - S(48), S(190));
    lv_obj_set_pos(hm.menu, S(24), S(44));
    lv_obj_set_flex_align(hm.menu, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    hm.beer = label(hm.menu, F_R46, C_INK);
    lv_obj_set_width(hm.beer, lv_pct(100));
    lv_label_set_long_mode(hm.beer, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(hm.beer, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_max_height(hm.beer, F_R46->line_height * 2, 0);
    hm.style = label(hm.menu, F_R18, C_INK2);
    lv_obj_t *pills = flex(hm.menu, LV_FLEX_FLOW_ROW, S(8));
    lv_obj_set_size(pills, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_top(pills, S(10), 0);
    const char *names[] = {"\xC3\x81lcool", "Amargor"};
    lv_obj_t **vals[] = {&hm.abv, &hm.ibu};
    for (int i = 0; i < 2; i++) {
      lv_obj_t *p = flex(pills, LV_FLEX_FLOW_ROW, S(8));
      lv_obj_set_size(p, LV_SIZE_CONTENT, S(36));
      lv_obj_set_style_radius(p, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(p, 1, 0);
      lv_obj_set_style_border_color(p, lv_color_hex(C_LINE), 0);
      lv_obj_set_style_pad_hor(p, S(16), 0);
      lv_obj_set_flex_align(p, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      label(p, F_R18, C_INK2, names[i]);
      *vals[i] = label(p, F_R18, C_INK);
    }
  } else {
    // O numero grande, como no app: inteiro branco, decimal cinza menor (sem virgula) na mesma linha de
    // base e "°C" pequeno e cinza na altura do topo dos algarismos
    hm.hero = flex(scr_home, LV_FLEX_FLOW_ROW, 0);
    lv_obj_set_size(hm.hero, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_align(hm.hero, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    const lv_font_t *fi = F_L146, *fd = F_L88, *fu = F_R22;
    const lv_coord_t ai = fi->line_height - fi->base_line, ad = fd->line_height - fd->base_line, au = fu->line_height - fu->base_line;
    hm.hInt = label(hm.hero, fi, C_INK, "--");
    lv_obj_set_style_text_letter_space(hm.hInt, -S(6), 0);
    hm.hDec = label(hm.hero, fd, C_GRAY, "");
    lv_obj_set_style_pad_left(hm.hDec, S(4), 0);
    lv_obj_set_style_translate_y(hm.hDec, ai - ad, 0); // mesma linha de base do inteiro
    hm.hUnit = label(hm.hero, fu, C_GRAY, "\xC2\xB0""C");
    lv_obj_set_style_pad_left(hm.hUnit, S(6), 0);
    // topo do "°C" no topo dos algarismos (altura de maiuscula da Outfit ~0,7 do tamanho)
    lv_obj_set_style_translate_y(hm.hUnit, (ai - (lv_coord_t)(0.70f * S(146))) - (au - (lv_coord_t)(0.70f * S(22))), 0);
    lv_obj_align(hm.hero, LV_ALIGN_TOP_MID, 0, S(172) - ai); // linha de base em y=172

    hm.state = flex(scr_home, LV_FLEX_FLOW_ROW, S(8));
    lv_obj_set_size(hm.state, LV_SIZE_CONTENT, S(36));
    lv_obj_set_style_radius(hm.state, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(hm.state, 1, 0);
    lv_obj_set_style_border_color(hm.state, lv_color_hex(C_LINE), 0);
    lv_obj_set_style_pad_hor(hm.state, S(16), 0);
    lv_obj_set_flex_align(hm.state, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    hm.stDot = dot(hm.state, S(9));
    hm.stWord = label(hm.state, F_R18, C_INK);
    lv_obj_align(hm.state, LV_ALIGN_TOP_MID, 0, S(182));
  }

  // Fileira de blocos na base
  hm.tiles = flex(scr_home, LV_FLEX_FLOW_ROW, S(8));
  lv_obj_set_size(hm.tiles, W - S(28), S(66));
  lv_obj_set_pos(hm.tiles, S(14), H - S(14) - S(66));
  for (int i = 0; i < 4; i++) {
    hm.t[i] = tile(hm.tiles, S(66));
    lv_obj_set_flex_grow(hm.t[i].o, 1);
    lv_obj_add_flag(hm.t[i].o, LV_OBJ_FLAG_HIDDEN);
  }
  build_sheet();
}

static void update_home() {
  if (!scr_home) return;
  if (hm.builtMode != currentMode || request_dashboard_refresh) {
    const bool wasActive = lv_scr_act() == scr_home;
    homeDirty = true;
    if (wasActive) { lv_obj_t *old = scr_home; build_home(); lv_scr_load(scr_home); lv_obj_del_async(old); }
    return;
  }
  if (sheet_signature() != hm.sheetSig) build_sheet();

  const RelayState rs = relay_state();
  const bool ok = control_sensor_ok();
  const float t = control_temp();
  const float tgt = shown_target();

  // Topo: aviso, modo, relogio
  const bool online = net_online();
  const bool rejected = online && key_rejected();
  set_hidden(hm.warn, online && !rejected);
  if (!online || rejected) {
    const uint32_t c = rejected ? C_RED : C_AMBER;
    set_border_color(hm.warn, c);
    lv_obj_t *wl = lv_obj_get_child(hm.warn, 0);
    set_text_color(wl, c);
    set_text(wl, rejected ? "v\xC3\xADnculo recusado" : "sem internet");
  }
  set_text(hm.topl, mode_name(currentMode));
  set_text(hm.net, net_eth_up() ? SYM_CABLE : (WiFi.status() == WL_CONNECTED ? SYM_WIFI : "")); // sem rede: so a pilula de aviso
  time_t now = time(nullptr);
  if (now > 1704067200) {
    struct tm ti;
    localtime_r(&now, &ti);
    char hhmm[8];
    snprintf(hhmm, sizeof(hhmm), "%02d:%02d", ti.tm_hour, ti.tm_min);
    set_text(hm.clock, hhmm);
  }

  // Linha compacta (3,5" com os controles abertos)
  set_text(lv_obj_get_child(hm.mini, 0), ok ? fmt1(t) + "\xC2\xB0" : String("--"));
  dot_set(lv_obj_get_child(hm.mini, 1), rs.color, rs.ring);
  set_text(lv_obj_get_child(hm.mini, 2), rs.word);

  if (hm.hero) {
    if (ok) {
      const float r = roundf(t * 10.0f) / 10.0f;
      const int ip = (int)r;
      const int dp = (int)roundf(fabsf(r - ip) * 10.0f) % 10;
      char a[8], b[4];
      snprintf(a, sizeof(a), "%s%d", (r < 0 && ip == 0) ? "-" : "", ip);
      snprintf(b, sizeof(b), "%d", dp);
      set_text(hm.hInt, a);
      set_text(hm.hDec, b);
      set_text_color(hm.hInt, C_INK);
    } else {
      set_text(hm.hInt, "--");
      set_text(hm.hDec, "");
      set_text_color(hm.hInt, C_GRAY);
    }
    dot_set(hm.stDot, rs.color, rs.ring);
    set_text(hm.stWord, rs.word);
    set_text_color(hm.stWord, rs.alert ? C_RED : C_INK);
    set_border_color(hm.state, rs.alert ? C_ALERT_LINE : C_LINE);
  }
  if (hm.menu) {
    set_text(hm.beer, beerName.length() ? beerName.c_str() : "Sem nome");
    set_text(hm.style, beerType);
    set_text(hm.abv, fmt1(beerABV) + "%");
    set_text(hm.ibu, String(beerIBU) + " IBU");
  }

  // Blocos
  int n = 0;
  bool dotOn[4] = {false, false, false, false};
  float barAt[4] = {-1, -1, -1, -1};
  uint8_t grow[4] = {1, 1, 1, 1};
  auto next = [&]() -> Tile & { Tile &x = hm.t[n++]; set_hidden(x.o, false); return x; };
  if (currentMode == MODE_CHOPP) {
    Tile &a = next(); if (ok) tile_set(a, "Temperatura", fmt1(t) + "\xC2\xB0", true); else tile_set(a, "Temperatura", "sem leitura", false, C_RED, true);
    Tile &b = next(); tile_set(b, "Alvo", fmt1(tgt) + "\xC2\xB0", true);
    Tile &c = next(); grow[2] = 2;
    if (ok) { tile_set(c, "Estado", rs.word, false); dotOn[2] = true; dot_set(c.dot, rs.color, rs.ring); }
    else tile_set(c, "Estado", "rel\xC3\xA9s desligados", false, C_RED, true);
    if (SIDE_SHEET && !isnan(currentHumidity)) { Tile &d = next(); tile_set(d, "Umidade", String((int)roundf(currentHumidity)) + "%", true); }
  } else {
    Tile &a = next(); tile_set(a, batch_active() ? "Alvo da etapa" : "Alvo", fmt1(tgt) + "\xC2\xB0", true);
    if (!ok) {
      Tile &b = next(); grow[1] = 2; tile_set(b, "Sensor de controle", "sem leitura \xC2\xB7 confira o cabo", false, C_RED, true);
    } else if (batch_active()) {
      Tile &b = next(); tile_set(b, "Densidade", ispindel_ok() ? String(gravity_sg(), 3) : String("--"), true);
      Tile &c = next(); grow[2] = 2; // o texto da rampa e mais largo
      if (!SIDE_SHEET) { grow[0] = 2; grow[1] = 2; grow[2] = 4; }
      char lb[48];
      snprintf(lb, sizeof(lb), "Rampa %d/%d \xC2\xB7 %s", currentRampStep + 1, numRampSteps,
               isRampPaused ? "pausada" : ("faltam " + dur_left(step_left_sec())).c_str());
      tile_set(c, lb, rampSteps[currentRampStep].name, false);
      barAt[2] = step_progress();
    } else if (currentMode == MODE_FERM) {
      Tile &b = next(); grow[1] = 2; tile_set(b, "Lote", "Nenhum ativo \xC2\xB7 comece no app", false);
    } else {
      Tile &b = next(); tile_set(b, "Umidade", isnan(currentHumidity) ? String("--") : String((int)roundf(currentHumidity)) + "%", true);
    }
    if (SIDE_SHEET && ok && !isnan(sensorTemps[1]) && sensor_fresh(1) && control_slot() != 1) { Tile &d = next(); tile_set(d, "Sensor 2", fmt1(sensorTemps[1]) + "\xC2\xB0", true); }
  }
  for (int i = 0; i < 4; i++) {
    set_hidden(hm.t[i].o, i >= n);
    if (i >= n) continue;
    set_grow(hm.t[i].o, grow[i]);
    set_hidden(hm.t[i].dot, !dotOn[i]);
    tile_bar(hm.t[i], barAt[i]);
  }

  // Folha
  if (hm.tgtVal) set_text(hm.tgtVal, fmt1(tgt) + "\xC2\xB0");
  if (hm.stepName && batch_active()) {
    set_text(hm.stepName, String(rampSteps[currentRampStep].name) + " \xC2\xB7 " + (currentRampStep + 1) + " de " + numRampSteps);
    set_text(hm.stepLeft, isRampPaused ? String("pausada") : "faltam " + dur_left(step_left_sec()));
    if (hm.pauseLbl) set_text(hm.pauseLbl, isRampPaused ? "Retomar" : "Pausar");
  } else if (hm.stepLeft && currentMode == MODE_GELADEIRA) {
    set_text(hm.stepLeft, isnan(currentHumidity) ? String("") : "Umidade " + String((int)roundf(currentHumidity)) + "%");
  }
  // Fecha sozinha depois de 8 s sem toque
  if (hm.open && !dlgRoot && lv_disp_get_inactive_time(NULL) > 8000) sheet_set_open(false);
}

// ---------------------------------------------------------------------------------------------
// Sensores e Aparelho
// ---------------------------------------------------------------------------------------------
static struct { lv_obj_t *scr; Tile t[8]; } sens;
static lv_obj_t *tile_grid(lv_obj_t *body) {
  lv_obj_t *g = flex(body, LV_FLEX_FLOW_ROW_WRAP, S(8));
  lv_obj_set_size(g, lv_pct(100), LV_SIZE_CONTENT);
  return g;
}
static lv_coord_t grid_w() { const int cols = SIDE_SHEET ? 3 : 2; return (W - S(40) - S(8) * (cols - 1)) / cols; }

static void update_sensors() {
  if (!sens.scr) return;
  const char *names[] = {"Sensor 1", "Sensor 2", "Sensor 3", "Sensor 4"};
  for (int k = 0; k < 4; k++) {
    Tile &x = sens.t[k];
    const bool ctl = control_slot() == k;
    if (!isnan(sensorTemps[k]) && sensor_fresh(k)) tile_set(x, names[k], fmt1(sensorTemps[k]) + "\xC2\xB0", true);
    else tile_set(x, names[k], k < 2 || ctl ? "sem leitura" : "n\xC3\xA3o conectado", false, ctl ? C_RED : C_INK2, ctl);
    set_text(x.sub, ctl ? "controla o modo" : "s\xC3\xB3 mostra");
  }
  tile_set(sens.t[4], "Umidade", isnan(currentHumidity) ? String("--") : String((int)roundf(currentHumidity)) + "%", true);
  tile_set(sens.t[5], "Dens\xC3\xADmetro", ispindel_ok() ? String(gravity_sg(), 3) : String("nenhum"), ispindel_ok());
  set_text(sens.t[5].sub, ispindel_ok() ? "iSpindel \xC2\xB7 bateria " + fmt1(ispindelBattery) + " V" : String("iSpindel"));
  for (int r = 0; r < 2; r++) {
    Tile &x = sens.t[6 + r];
    const bool on = r ? isHeaterOn : isCoolerOn;
    tile_set(x, r ? "Rel\xC3\xA9 de calor" : "Rel\xC3\xA9 de frio", on ? "ligado" : "desligado", false);
    set_hidden(x.dot, !on);
    if (on) dot_set(x.dot, r ? C_AMBER : C_COOL, false);
  }
}
static void open_sensors() {
  Page p = page("Sensores", go_home_cb);
  sens.scr = p.scr;
  lv_obj_add_event_cb(p.scr, [](lv_event_t *e) { if (lv_event_get_target(e) == sens.scr) memset(&sens, 0, sizeof(sens)); }, LV_EVENT_DELETE, NULL);
  lv_obj_t *g = tile_grid(p.body);
  const char *subs[] = {"", "", "", "", "DHT22", "", "compressor", "resist\xC3\xAAncia"};
  for (int i = 0; i < 8; i++) {
    sens.t[i] = tile(g, S(70));
    lv_obj_set_width(sens.t[i].o, grid_w());
    lv_obj_clear_flag(sens.t[i].sub, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(sens.t[i].sub, subs[i]);
  }
  update_sensors();
  show(p.scr);
}

static struct { lv_obj_t *scr; Tile net, code; } devp;
static void wifi_change_apply();
static void wifi_change_cb(lv_event_t *) {
  dialog("Trocar a rede Wi-Fi?", "O controle continua funcionando enquanto voc\xC3\xAA escolhe a nova rede.", "Escolher rede", wifi_change_apply);
}
static void update_device() {
  if (!devp.scr) return;
  tile_set(devp.net, net_eth_up() ? "Rede" : "Rede Wi-Fi",
           net_eth_up() ? String("Cabo de rede") : (WiFi.status() == WL_CONNECTED ? WiFi.SSID() : String("desconectado")), false);
  String code;
  tile_set(devp.code, "C\xC3\xB3""digo de confirma\xC3\xA7\xC3\xA3o", cloud_pair_code(code) ? code : String("aguardando rede"), false,
           key_rejected() ? C_RED : C_INK, key_rejected());
}
static void open_device() {
  Page p = page("Aparelho", go_home_cb);
  devp.scr = p.scr;
  lv_obj_add_event_cb(p.scr, [](lv_event_t *e) { if (lv_event_get_target(e) == devp.scr) memset(&devp, 0, sizeof(devp)); }, LV_EVENT_DELETE, NULL);
  lv_obj_t *g = tile_grid(p.body);
  Tile a = tile(g, S(70)); tile_set(a, "Aparelho", get_device_id(), false);
  devp.net = tile(g, S(70));
  Tile f = tile(g, S(70)); tile_set(f, "Firmware", String(FW_VERSION) + " \xC2\xB7 " + BREWW_BOARD_NAME, false);
  devp.code = tile(g, S(70));
  lv_obj_t *all[] = {a.o, devp.net.o, f.o, devp.code.o};
  for (lv_obj_t *o : all) lv_obj_set_width(o, grid_w());
  lv_obj_t *acts = flex(p.body, LV_FLEX_FLOW_ROW, S(8));
  lv_obj_set_size(acts, lv_pct(100), LV_SIZE_CONTENT);
  lv_obj_set_style_pad_top(acts, S(2), 0);
  lv_obj_t *wb = button(acts, "Trocar rede Wi-Fi", BTN_SOFT, wifi_change_cb);
  lv_obj_set_flex_grow(wb, 1);
  lv_obj_t *note = label(p.body, F_R13, C_INK2,
                         "Perfis, alertas, calibra\xC3\xA7\xC3\xA3o e ajustes finos ficam no app e em breww.live. "
                         "Se o v\xC3\xADnculo for recusado, adicione o aparelho de novo no app com o c\xC3\xB3""digo de confirma\xC3\xA7\xC3\xA3o.");
  lv_obj_set_width(note, lv_pct(100));
  lv_label_set_long_mode(note, LV_LABEL_LONG_WRAP);
  update_device();
  show(p.scr);
}

// ---------------------------------------------------------------------------------------------
// Teclado (senha do Wi-Fi e cardapio da choppeira)
// ---------------------------------------------------------------------------------------------
static const char *KB_LOWER[] = {"q","w","e","r","t","y","u","i","o","p","\n","a","s","d","f","g","h","j","k","l","\n",
                                 SYM_SHIFT,"z","x","c","v","b","n","m",SYM_BACKSPACE,"\n","123"," ","OK",""};
static const char *KB_UPPER[] = {"Q","W","E","R","T","Y","U","I","O","P","\n","A","S","D","F","G","H","J","K","L","\n",
                                 SYM_SHIFT,"Z","X","C","V","B","N","M",SYM_BACKSPACE,"\n","123"," ","OK",""};
static const char *KB_NUM[] = {"1","2","3","4","5","6","7","8","9","0","\n","@","#","$","%","&","*","-","+","(",")","\n",
                               "!","?",".",",",":",";","/","_","\xC3\xA7",SYM_BACKSPACE,"\n","abc"," ","OK",""};
// Teclas de trocar o modo (shift, 123, abc) e a de confirmar: disparam ao SOLTAR e nao repetem. Antes
// disparavam ao tocar: o "123" virava o teclado numerico com o dedo ainda na tela e, no mesmo lugar, o
// "abc" disparava (toque um pouco mais longo ou dedo lido duas vezes no 7") e voltava para as letras.
#define KB_ONCE (LV_BTNMATRIX_CTRL_NO_REPEAT | LV_BTNMATRIX_CTRL_CLICK_TRIG)
static const lv_btnmatrix_ctrl_t KB_CTRL_ALPHA[] = {2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2, (lv_btnmatrix_ctrl_t)(3 | KB_ONCE),2,2,2,2,2,2,2,3,
                                                     (lv_btnmatrix_ctrl_t)(3 | KB_ONCE),8,(lv_btnmatrix_ctrl_t)(4 | LV_BTNMATRIX_CTRL_CHECKED | KB_ONCE)};
static const lv_btnmatrix_ctrl_t KB_CTRL_NUM[] = {2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,2,3,
                                                   (lv_btnmatrix_ctrl_t)(3 | KB_ONCE),8,(lv_btnmatrix_ctrl_t)(4 | LV_BTNMATRIX_CTRL_CHECKED | KB_ONCE)};

struct Kb { lv_obj_t *ta, *bm; int mode; void (*done)(const String &); };
static Kb kb;
static char kbOk[16] = "OK";        // rotulo do botao de confirmar ("Conectar", "Pronto")
static const char *kbMaps[3][40];   // mapas com o rotulo trocado
static void kb_set_mode(int m) {
  kb.mode = m;
  lv_btnmatrix_set_map(kb.bm, kbMaps[m == 2 ? 2 : (m == 1 ? 1 : 0)]);
  lv_btnmatrix_set_ctrl_map(kb.bm, m == 2 ? KB_CTRL_NUM : KB_CTRL_ALPHA);
}
static void kb_cb(lv_event_t *) {
  const uint16_t id = lv_btnmatrix_get_selected_btn(kb.bm);
  if (id == LV_BTNMATRIX_BTN_NONE) return;
  const char *t = lv_btnmatrix_get_btn_text(kb.bm, id);
  if (!t) return;
  if (!strcmp(t, kbOk)) { if (kb.done) kb.done(String(lv_textarea_get_text(kb.ta))); return; }
  // Segunda troca de modo em menos de 350 ms = o mesmo toque lido de novo: ignora
  static uint32_t lastSwitch = 0;
  const bool isSwitch = !strcmp(t, SYM_SHIFT) || !strcmp(t, "123") || !strcmp(t, "abc");
  if (isSwitch) {
    if (lv_tick_elaps(lastSwitch) < 350) return;
    lastSwitch = lv_tick_get();
  }
  if (!strcmp(t, SYM_SHIFT)) { kb_set_mode(kb.mode == 1 ? 0 : 1); return; }
  if (!strcmp(t, "123")) { kb_set_mode(2); return; }
  if (!strcmp(t, "abc")) { kb_set_mode(0); return; }
  if (!strcmp(t, SYM_BACKSPACE)) { lv_textarea_del_char(kb.ta); return; }
  lv_textarea_add_text(kb.ta, t);
  if (kb.mode == 1) kb_set_mode(0); // maiuscula so na proxima letra
}
// Campo + teclado dentro de uma pagina
static void keyboard(lv_obj_t *body, const String &initial, bool password, const char *okText, void (*done)(const String &)) {
  memset(&kb, 0, sizeof(kb));
  kb.done = done;
  kb.ta = lv_textarea_create(body);
  lv_obj_remove_style_all(kb.ta);
  fill_block(kb.ta, S(14));
  lv_obj_set_style_border_color(kb.ta, lv_color_hex(0x3E7A4D), 0);
  lv_obj_set_style_pad_hor(kb.ta, S(14), 0);
  lv_obj_set_style_pad_ver(kb.ta, S(9), 0);
  lv_obj_set_style_text_font(kb.ta, F_R18, 0);
  lv_obj_set_style_text_color(kb.ta, lv_color_hex(C_INK), 0);
  lv_obj_set_style_border_color(kb.ta, lv_color_hex(C_GREEN), LV_PART_CURSOR);
  lv_obj_set_style_border_width(kb.ta, S(2), LV_PART_CURSOR);
  lv_obj_set_style_border_side(kb.ta, LV_BORDER_SIDE_LEFT, LV_PART_CURSOR);
  lv_obj_set_width(kb.ta, lv_pct(100));
  lv_textarea_set_one_line(kb.ta, true);
  lv_textarea_set_password_mode(kb.ta, password);
  if (password) lv_textarea_set_password_show_time(kb.ta, 1000);
  lv_textarea_set_text(kb.ta, initial.c_str());
  lv_obj_add_state(kb.ta, LV_STATE_FOCUSED); // cursor piscando
  lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  kb.bm = lv_btnmatrix_create(body);
  lv_obj_remove_style_all(kb.bm);
  lv_obj_set_width(kb.bm, lv_pct(100));
  lv_obj_set_flex_grow(kb.bm, 1);
  lv_obj_set_style_pad_row(kb.bm, S(4), 0);
  lv_obj_set_style_pad_column(kb.bm, S(4), 0);
  lv_obj_set_style_bg_opa(kb.bm, LV_OPA_COVER, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb.bm, lv_color_hex(C_BLOCK), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb.bm, lv_color_hex(0x2A3040), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_border_width(kb.bm, 1, LV_PART_ITEMS);
  lv_obj_set_style_border_color(kb.bm, lv_color_hex(C_LINE), LV_PART_ITEMS);
  lv_obj_set_style_radius(kb.bm, S(10), LV_PART_ITEMS);
  lv_obj_set_style_text_font(kb.bm, F_R18, LV_PART_ITEMS);
  lv_obj_set_style_text_color(kb.bm, lv_color_hex(C_INK), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(kb.bm, lv_color_hex(C_GREEN), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(kb.bm, lv_color_hex(C_ONGRN), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_border_color(kb.bm, lv_color_hex(C_GREEN), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_add_event_cb(kb.bm, kb_cb, LV_EVENT_VALUE_CHANGED, NULL);
  snprintf(kbOk, sizeof(kbOk), "%s", okText);
  const char **src[3] = {KB_LOWER, KB_UPPER, KB_NUM};
  for (int m = 0; m < 3; m++) {
    int i = 0;
    for (; src[m][i][0]; i++) kbMaps[m][i] = strcmp(src[m][i], "OK") ? src[m][i] : kbOk;
    kbMaps[m][i] = "";
  }
  kb_set_mode(0);
}

// ---------------------------------------------------------------------------------------------
// Choppeira: dados da cerveja
// ---------------------------------------------------------------------------------------------
static struct { String name, type; int ibu; float abv; int field; } beerEdit;
static void open_beer_page();
static void beer_kb_done(const String &v) {
  (beerEdit.field == 0 ? beerEdit.name : beerEdit.type) = v;
  open_beer_page();
}
static void beer_field_cb(lv_event_t *e) {
  beerEdit.field = (int)(intptr_t)lv_event_get_user_data(e);
  Page p = page(beerEdit.field == 0 ? "Nome da cerveja" : "Estilo", [](lv_event_t *) { open_beer_page(); });
  keyboard(p.body, beerEdit.field == 0 ? beerEdit.name : beerEdit.type, false, "Pronto", beer_kb_done);
  show(p.scr);
}
static void beer_step_cb(lv_event_t *e) {
  const int k = (int)(intptr_t)lv_event_get_user_data(e);
  if (k == 0) beerEdit.abv = max(0.0f, roundf((beerEdit.abv - 0.1f) * 10) / 10);
  if (k == 1) beerEdit.abv = min(20.0f, roundf((beerEdit.abv + 0.1f) * 10) / 10);
  if (k == 2) beerEdit.ibu = max(0, beerEdit.ibu - 1);
  if (k == 3) beerEdit.ibu = min(120, beerEdit.ibu + 1);
  lv_obj_t *v = (lv_obj_t *)lv_obj_get_user_data(lv_event_get_target(e));
  if (k < 2) lv_label_set_text(v, fmt1(beerEdit.abv).c_str()); else lv_label_set_text_fmt(v, "%d", beerEdit.ibu);
}
static void beer_save_cb(lv_event_t *) {
  settings_set_beer_info(beerEdit.name, beerEdit.type, beerEdit.ibu, beerEdit.abv); // grava e publica
  homeDirty = true;
  go_home();
  toast("Card\xC3\xA1pio atualizado");
}
static void open_beer() {
  beerEdit.name = beerName; beerEdit.type = beerType; beerEdit.ibu = beerIBU; beerEdit.abv = beerABV;
  open_beer_page();
}
static void open_beer_page() {
  Page p = page("Cerveja na torneira", go_home_cb);
  lv_obj_t *save = button(p.hd, "Salvar", BTN_PRIMARY, beer_save_cb);
  (void)save;
  const char *lbs[] = {"Nome", "Estilo"};
  const String *vals[] = {&beerEdit.name, &beerEdit.type};
  for (int i = 0; i < 2; i++) {
    lv_obj_t *it = flex(p.body, LV_FLEX_FLOW_ROW, S(12));
    fill_block(it, S(16));
    lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(it, lv_color_hex(C_BLOCK2), LV_STATE_PRESSED);
    lv_obj_set_size(it, lv_pct(100), S(54));
    lv_obj_set_style_pad_hor(it, S(14), 0);
    lv_obj_set_flex_align(it, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(it, beer_field_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *col = flex(it, LV_FLEX_FLOW_COLUMN, 0);
    lv_obj_set_height(col, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(col, 1);
    label(col, F_R12, C_INK2, lbs[i]);
    lv_obj_t *v = label(col, F_R15, C_INK, vals[i]->length() ? vals[i]->c_str() : "\xE2\x80\x94");
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_width(v, lv_pct(100));
    label(it, F_R13, C_INK2, "Editar");
  }
  lv_obj_t *row = flex(p.body, LV_FLEX_FLOW_ROW, S(8));
  lv_obj_set_size(row, lv_pct(100), LV_SIZE_CONTENT);
  const char *caps[] = {"\xC3\x81lcool %", "IBU"};
  for (int i = 0; i < 2; i++) {
    lv_obj_t *b = flex(row, LV_FLEX_FLOW_COLUMN, S(4));
    fill_block(b, S(18));
    lv_obj_set_height(b, LV_SIZE_CONTENT);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_pad_all(b, S(8), 0);
    lv_obj_set_flex_align(b, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    label(b, F_R12, C_INK2, caps[i]);
    lv_obj_t *st = flex(b, LV_FLEX_FLOW_ROW, S(6));
    lv_obj_set_size(st, lv_pct(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_align(st, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_t *mn = button(st, "\xE2\x88\x92", BTN_SOFT, beer_step_cb, (void *)(intptr_t)(i * 2), F_R22);
    lv_obj_t *v = label(st, F_L34, C_INK, i == 0 ? fmt1(beerEdit.abv).c_str() : String(beerEdit.ibu).c_str());
    lv_obj_t *pl = button(st, "+", BTN_SOFT, beer_step_cb, (void *)(intptr_t)(i * 2 + 1), F_R22);
    lv_obj_t *bs[] = {mn, pl};
    for (lv_obj_t *x : bs) { lv_obj_set_size(x, S(48), S(48)); lv_obj_set_style_pad_hor(x, 0, 0); lv_obj_set_user_data(x, v); }
  }
  show(p.scr);
}

// ---------------------------------------------------------------------------------------------
// Assistente de primeira instalacao: boas-vindas -> Wi-Fi -> senha -> conectando -> vinculo -> pronto
// ---------------------------------------------------------------------------------------------
static bool wizFromDevice = false; // trocando de rede pelo "Aparelho" (volta para a leitura no fim)
static String wizSsid;

static lv_obj_t *steps_dots(lv_obj_t *parent, int n) {
  lv_obj_t *r = flex(parent, LV_FLEX_FLOW_ROW, S(6));
  lv_obj_set_size(r, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  for (int i = 1; i <= 3; i++) {
    lv_obj_t *d = box(r);
    lv_obj_set_size(d, S(18), S(4));
    lv_obj_set_style_radius(d, S(2), 0);
    lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(d, i <= n ? lv_color_hex(C_GREEN) : lv_color_hex(0x24272E), 0);
  }
  return r;
}
static lv_obj_t *center_col(lv_obj_t *scr) {
  lv_obj_t *c = flex(scr, LV_FLEX_FLOW_COLUMN, S(10));
  lv_obj_set_size(c, W - S(40), H);
  lv_obj_center(c);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  return c;
}
static lv_obj_t *wrap_text(lv_obj_t *parent, const lv_font_t *f, uint32_t c, const char *t, lv_coord_t w) {
  lv_obj_t *l = label(parent, f, c, t);
  lv_obj_set_width(l, w);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  return l;
}

static void open_wifi();
static void open_pass();
static void open_connect();
static void open_pair();
static void open_ready();

static void open_welcome() {
  lv_obj_t *s = new_screen();
  lv_obj_t *c = center_col(s);
  lv_obj_t *img = lv_img_create(c);
  lv_img_set_src(img, IMG_WM_SPLASH);
  lv_obj_set_style_img_recolor(img, lv_color_hex(C_INK), 0);
  lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_bottom(img, S(6), 0);
  label(c, F_R28, C_INK, "Vamos ligar seu Breww");
  wrap_text(c, F_R15, C_INK2, "Tr\xC3\xAAs passos: Wi-Fi, vincular \xC3\xA0 sua conta e pronto. Leva uns dois minutos.", S(380));
  lv_obj_t *b = button(c, "Come\xC3\xA7""ar", BTN_PRIMARY, [](lv_event_t *) {
    wizFromDevice = false;
    if (net_eth_up()) open_pair(); else open_wifi(); // com cabo de rede, o Wi-Fi nao e necessario
  });
  lv_obj_set_width(b, S(200));
  show(s);
}

static struct { lv_obj_t *scr, *list, *wait; bool listed; } wf;
static void wifi_item_cb(lv_event_t *e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  wizSsid = get_scanned_ssid(i);
  if (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) { wifi_setup_connect_new(wizSsid, ""); open_connect(); }
  else open_pass();
}
static void wifi_fill(int n) {
  lv_obj_clean(wf.list);
  wf.listed = true;
  if (n <= 0) {
    label(wf.list, F_R15, C_INK2, "Nenhuma rede encontrada.");
  }
  for (int i = 0; i < n && i < 20; i++) {
    const String ssid = get_scanned_ssid(i);
    if (!ssid.length()) continue;
    lv_obj_t *it = flex(wf.list, LV_FLEX_FLOW_ROW, S(12));
    fill_block(it, S(16));
    lv_obj_add_flag(it, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_color(it, lv_color_hex(C_BLOCK2), LV_STATE_PRESSED);
    lv_obj_set_size(it, lv_pct(100), S(52));
    lv_obj_set_style_pad_hor(it, S(14), 0);
    lv_obj_set_flex_align(it, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_add_event_cb(it, wifi_item_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *nm = label(it, F_R15, C_INK, ssid.c_str());
    lv_obj_set_flex_grow(nm, 1);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    label(it, F_R13, C_INK2, WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "aberta" : "senha");
    // Barras de sinal (4), as acesas em branco
    const int r = WiFi.RSSI(i), lit = r > -60 ? 4 : r > -70 ? 3 : r > -80 ? 2 : 1;
    lv_obj_t *bars = flex(it, LV_FLEX_FLOW_ROW, S(2));
    lv_obj_set_size(bars, LV_SIZE_CONTENT, S(14));
    lv_obj_set_flex_align(bars, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
    for (int k = 1; k <= 4; k++) {
      lv_obj_t *bar = box(bars);
      lv_obj_set_size(bar, S(3), S(14) * k / 4);
      lv_obj_set_style_radius(bar, 1, 0);
      lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(bar, k <= lit ? lv_color_hex(C_INK) : lv_color_hex(0x33363D), 0);
    }
  }
  button(wf.list, "Procurar de novo", BTN_GHOST, [](lv_event_t *) { open_wifi(); });
}
static void open_wifi() {
  Page p = page("Escolha sua rede Wi-Fi", wizFromDevice ? go_home_cb : NULL);
  if (!wizFromDevice) steps_dots(p.hd, 1);
  wf.scr = p.scr;
  wf.listed = false;
  lv_obj_add_event_cb(p.scr, [](lv_event_t *e) { if (lv_event_get_target(e) == wf.scr) memset(&wf, 0, sizeof(wf)); }, LV_EVENT_DELETE, NULL);
  wf.list = p.body;
  wf.wait = center_col(p.body);
  lv_obj_set_size(wf.wait, lv_pct(100), lv_pct(100));
  lv_obj_t *sp = lv_spinner_create(wf.wait, 900, 60);
  lv_obj_set_size(sp, S(36), S(36));
  lv_obj_set_style_arc_width(sp, S(3), 0);
  lv_obj_set_style_arc_width(sp, S(3), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(sp, lv_color_hex(0x24272E), 0);
  lv_obj_set_style_arc_color(sp, lv_color_hex(C_GREEN), LV_PART_INDICATOR);
  label(wf.wait, F_R15, C_INK2, "Procurando redes\xE2\x80\xA6");
  start_wifi_scan();
  show(p.scr);
}
static void update_wifi() {
  if (!wf.scr || wf.listed) return;
  const int n = check_wifi_scan();
  if (n >= 0) wifi_fill(n);
}

static void pass_done(const String &pass) {
  wifi_setup_connect_new(wizSsid, pass); // grava e conecta
  open_connect();
}
static void open_pass() {
  char t[64];
  snprintf(t, sizeof(t), "Senha de \xE2\x80\x9C%s\xE2\x80\x9D", wizSsid.c_str());
  Page p = page(t, [](lv_event_t *) { open_wifi(); });
  keyboard(p.body, "", true, "Conectar", pass_done);
  show(p.scr);
}

static struct { lv_obj_t *scr; } cn;
static void open_connect() {
  lv_obj_t *s = new_screen();
  cn.scr = s;
  lv_obj_add_event_cb(s, [](lv_event_t *e) { if (lv_event_get_target(e) == cn.scr) cn.scr = NULL; }, LV_EVENT_DELETE, NULL);
  lv_obj_t *c = center_col(s);
  lv_obj_t *sp = lv_spinner_create(c, 900, 60);
  lv_obj_set_size(sp, S(36), S(36));
  lv_obj_set_style_arc_width(sp, S(3), 0);
  lv_obj_set_style_arc_width(sp, S(3), LV_PART_INDICATOR);
  lv_obj_set_style_arc_color(sp, lv_color_hex(0x24272E), 0);
  lv_obj_set_style_arc_color(sp, lv_color_hex(C_GREEN), LV_PART_INDICATOR);
  label(c, F_R28, C_INK, "Conectando");
  char d[64];
  snprintf(d, sizeof(d), "\xC3\xA0 rede \xE2\x80\x9C%s\xE2\x80\x9D", wizSsid.c_str());
  label(c, F_R15, C_INK2, d);
  show(s);
}
static void connect_failed() {
  lv_obj_t *s = new_screen();
  lv_obj_t *c = center_col(s);
  label(c, F_R28, C_INK, "N\xC3\xA3o conectou");
  char d[140];
  snprintf(d, sizeof(d), "A rede \xE2\x80\x9C%s\xE2\x80\x9D n\xC3\xA3o aceitou. Confira a senha (no m\xC3\xADnimo 8 caracteres) e se o roteador est\xC3\xA1 perto.", wizSsid.c_str());
  wrap_text(c, F_R15, C_INK2, d, S(380));
  lv_obj_t *b = button(c, "Digitar de novo", BTN_PRIMARY, [](lv_event_t *) { open_pass(); });
  lv_obj_set_width(b, S(200));
  button(c, "Escolher outra rede", BTN_GHOST, [](lv_event_t *) { open_wifi(); });
  show(s);
}
static void update_connect() {
  if (!cn.scr) return;
  const WifiSetupState st = wifi_setup_poll();
  if (st == WIFI_SETUP_CONNECTED) {
    wifi_setup_reset();
    if (wizFromDevice || cloud_is_paired()) { go_home(); toast("Conectado"); }
    else open_pair();
  } else if (st == WIFI_SETUP_FAILED) {
    wifi_setup_reset();
    connect_failed();
  }
}

static struct { lv_obj_t *scr, *qr, *code, *wait, *other; bool hasCode; } pr;
static void open_pair() {
  Page p = page("Vincule \xC3\xA0 sua conta", NULL);
  steps_dots(p.hd, 2);
  pr.scr = p.scr;
  pr.hasCode = false;
  lv_obj_add_event_cb(p.scr, [](lv_event_t *e) { if (lv_event_get_target(e) == pr.scr) memset(&pr, 0, sizeof(pr)); }, LV_EVENT_DELETE, NULL);
  lv_obj_set_flex_flow(p.body, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(p.body, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(p.body, S(20), 0);
  lv_obj_t *qbox = box(p.body);
  lv_obj_set_style_bg_color(qbox, lv_color_white(), 0);
  lv_obj_set_style_bg_opa(qbox, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(qbox, S(14), 0);
  lv_obj_set_style_pad_all(qbox, S(8), 0);
  lv_obj_set_size(qbox, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  pr.qr = lv_qrcode_create(qbox, S(132), lv_color_hex(C_GROUND), lv_color_white());
  const String url = "https://breww.live/?pair=" + get_device_id();
  lv_qrcode_update(pr.qr, url.c_str(), url.length());
  lv_obj_t *col = flex(p.body, LV_FLEX_FLOW_COLUMN, S(4));
  lv_obj_set_height(col, LV_SIZE_CONTENT);
  lv_obj_set_flex_grow(col, 1);
  label(col, F_R12, C_INK2, "C\xC3\xB3""digo de confirma\xC3\xA7\xC3\xA3o");
  pr.code = label(col, F_C40, C_INK, "--------");
  lv_obj_set_style_text_letter_space(pr.code, S(4), 0);
  lv_obj_t *d = label(col, F_R15, C_INK2, "No app: Aparelhos \xE2\x80\xBA Adicionar. Ou em breww.live, com este c\xC3\xB3""digo.");
  lv_obj_set_width(d, lv_pct(100));
  lv_label_set_long_mode(d, LV_LABEL_LONG_WRAP);
  label(col, F_R12, C_GRAY, ("Aparelho " + get_device_id()).c_str());
  pr.wait = label(col, F_R13, C_INK2, "Esperando o pedido do app\xE2\x80\xA6");
  lv_obj_set_style_pad_top(pr.wait, S(6), 0);
  pr.other = button(col, "Escolher outra rede", BTN_GHOST, [](lv_event_t *) { wizFromDevice = false; open_wifi(); });
  lv_obj_set_style_pad_hor(pr.other, 0, 0);
  lv_obj_add_flag(pr.other, LV_OBJ_FLAG_HIDDEN);
  show(p.scr);
}
static void update_pair() {
  if (!pr.scr) return;
  String code;
  if (!pr.hasCode && cloud_pair_code(code)) {
    pr.hasCode = true;
    lv_label_set_text(pr.code, code.c_str());
    const String url = "https://breww.live/?pair=" + get_device_id() + "&code=" + code;
    lv_qrcode_update(pr.qr, url.c_str(), url.length());
  }
  const bool on = net_online();
  lv_label_set_text(pr.wait, on ? "Esperando o pedido do app\xE2\x80\xA6" : "Sem rede: confira o Wi-Fi ou o cabo.");
  if (on) lv_obj_add_flag(pr.other, LV_OBJ_FLAG_HIDDEN); else lv_obj_clear_flag(pr.other, LV_OBJ_FLAG_HIDDEN);
  if (cloud_is_paired()) open_ready(); // vinculado pelo app (o servidor criou a credencial)
}
static void open_ready() {
  lv_obj_t *s = new_screen();
  lv_obj_t *c = center_col(s);
  steps_dots(c, 3);
  label(c, F_R28, C_INK, "Tudo pronto");
  wrap_text(c, F_R15, C_INK2, "O Breww est\xC3\xA1 na sua conta e online. O controle j\xC3\xA1 come\xC3\xA7ou; toque na leitura para ajustar.", S(380));
  lv_obj_t *b = button(c, "Ir para a leitura", BTN_PRIMARY, go_home_cb);
  lv_obj_set_width(b, S(200));
  show(s);
}
static void wifi_change_apply() { wizFromDevice = true; open_wifi(); }

// ---------------------------------------------------------------------------------------------
// Atualizacao de firmware (sobre qualquer tela)
// ---------------------------------------------------------------------------------------------
static struct { lv_obj_t *root, *pct, *bar, *sub; int last; } ota = {NULL, NULL, NULL, NULL, -1};
void ui_show_ota_screen() {
  if (ota.root) return;
  dlg_close();
  ota.root = box(lv_layer_top());
  lv_obj_add_flag(ota.root, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(ota.root, W, H);
  lv_obj_set_style_bg_color(ota.root, lv_color_hex(C_GROUND), 0);
  lv_obj_set_style_bg_opa(ota.root, LV_OPA_COVER, 0);
  lv_obj_t *c = center_col(ota.root);
  label(c, F_R13, C_INK2, "Atualizando o firmware");
  ota.pct = label(c, F_L56, C_INK, "0%");
  ota.bar = lv_bar_create(c);
  lv_obj_remove_style_all(ota.bar);
  lv_obj_set_size(ota.bar, S(300), S(8));
  lv_obj_set_style_radius(ota.bar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(ota.bar, 20, 0);
  lv_obj_set_style_bg_color(ota.bar, lv_color_white(), 0);
  lv_obj_set_style_radius(ota.bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
  lv_obj_set_style_bg_opa(ota.bar, LV_OPA_COVER, LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(ota.bar, lv_color_hex(C_GREEN), LV_PART_INDICATOR);
  ota.sub = wrap_text(c, F_R15, C_INK2, "N\xC3\xA3o desligue o aparelho. O controle volta sozinho ao terminar.", S(380));
  ota.last = -1;
  lv_refr_now(NULL);
}
void ui_update_ota_progress(int percent) {
  if (!ota.root || percent == ota.last) return;
  ota.last = percent;
  lv_label_set_text_fmt(ota.pct, "%d%%", percent);
  lv_bar_set_value(ota.bar, percent, LV_ANIM_OFF);
  lv_refr_now(NULL);
}
void ui_ota_status(const char *text) {
  if (!ota.root) return;
  lv_label_set_text(ota.sub, text);
  ota.last = -1;
  lv_bar_set_value(ota.bar, 0, LV_ANIM_OFF);
  lv_label_set_text(ota.pct, "0%");
  lv_refr_now(NULL);
}
void ui_hide_ota_screen() {
  if (ota.root) lv_obj_del(ota.root);
  ota = {NULL, NULL, NULL, NULL, -1};
  lv_refr_now(NULL);
}
void ui_show_ota_success(bool success) {
  if (!ota.root) return;
  lv_obj_clean(ota.root);
  lv_obj_t *c = center_col(ota.root);
  label(c, F_R28, C_INK, success ? "Atualizado" : "N\xC3\xA3o foi poss\xC3\xADvel atualizar");
  wrap_text(c, F_R15, C_INK2, success ? "Reiniciando para usar a vers\xC3\xA3o nova." :
            "A vers\xC3\xA3o nova n\xC3\xA3o baixou. O Breww segue funcionando na vers\xC3\xA3o atual.", S(380));
  ota.pct = ota.bar = ota.sub = NULL;
  lv_refr_now(NULL);
  delay(2000); // segura a mensagem antes de reiniciar/voltar
}

// ---------------------------------------------------------------------------------------------
// Pedido de vinculo vindo do app (confirmacao na tela)
// ---------------------------------------------------------------------------------------------
static lv_timer_t *pairTimer = NULL;
static int pairLeft = 0;
static void pair_answer(bool accept) {
  if (pairTimer) { lv_timer_del(pairTimer); pairTimer = NULL; }
  cloud_pairing_respond(accept);
}
void ui_show_pair_confirm(const char *email, const char *deviceName, int expiresIn) {
  static char text[200];
  snprintf(text, sizeof(text), "%s pediu para adicionar este aparelho%s%s%s. Confira se foi voc\xC3\xAA.", email,
           deviceName && *deviceName ? " como \xE2\x80\x9C" : "", deviceName ? deviceName : "", deviceName && *deviceName ? "\xE2\x80\x9D" : "");
  dlg_close();
  dialog("Vincular a uma conta?", text, "Confirmar", [] { pair_answer(true); }, false, "Recusar", [] { pair_answer(false); });
  dlgIsPair = true;
  pairLeft = expiresIn > 0 ? expiresIn : 120;
  if (pairTimer) lv_timer_del(pairTimer);
  pairTimer = lv_timer_create([](lv_timer_t *) { if (--pairLeft <= 0) { ui_hide_pair_confirm(); } }, 1000, NULL);
}
void ui_hide_pair_confirm() {
  if (pairTimer) { lv_timer_del(pairTimer); pairTimer = NULL; }
  if (dlgIsPair) dlg_close();
}
void ui_pair_result(const char *status, bool requestOk) {
  const char *text;
  if (!requestOk) text = "Sem conex\xC3\xA3o com o servidor. Tente de novo pelo app.";
  else if (!strcmp(status, "confirmed")) text = "Aparelho vinculado";
  else if (!strcmp(status, "rejected")) text = "Pedido recusado";
  else if (!strcmp(status, "conflict")) text = "Este aparelho j\xC3\xA1 est\xC3\xA1 em outra conta";
  else if (!strcmp(status, "limit")) text = "A conta chegou ao limite de aparelhos";
  else text = "O pedido expirou. Tente de novo pelo app.";
  toast(text);
}

// ---------------------------------------------------------------------------------------------
// Ciclo
// ---------------------------------------------------------------------------------------------
void ui_update_temperatures() {}
void ui_update_relays() {}
void ui_update_ispindel() {}

static lv_obj_t *scr_splash = NULL;
static void ui_tick(lv_timer_t *) {
  target_commit();
  lv_obj_t *cur = lv_scr_act();
  if (cur == scr_home) update_home();
  else if (sens.scr && cur == sens.scr) update_sensors();
  else if (devp.scr && cur == devp.scr) update_device();
  else if (wf.scr && cur == wf.scr) update_wifi();
  else if (cn.scr && cur == cn.scr) update_connect();
  else if (pr.scr && cur == pr.scr) update_pair();
  if (cur != scr_home && scr_home && (hm.builtMode != currentMode || request_dashboard_refresh)) homeDirty = true;
}

void init_ui() {
  Serial.println(F("Inicializando LVGL (Interface Visual)..."));
  ui_display_begin();
  lv_obj_set_style_bg_color(lv_layer_top(), lv_color_hex(C_GROUND), 0);
  lv_obj_set_style_bg_opa(lv_layer_top(), LV_OPA_TRANSP, 0);

  scr_splash = new_screen();
  lv_obj_t *img = lv_img_create(scr_splash);
  lv_img_set_src(img, IMG_WM_SPLASH);
  lv_obj_set_style_img_recolor(img, lv_color_hex(C_INK), 0);
  lv_obj_set_style_img_recolor_opa(img, LV_OPA_COVER, 0);
  lv_obj_center(img);
  lv_scr_load(scr_splash);
  // Desenha a abertura ja: a rede leva ~2 s para iniciar e trava o loop
  lv_refr_now(NULL);
  lv_timer_create(ui_tick, 500, NULL);
}

void loop_ui() {
  lv_timer_handler();
  static bool booted = false;
  if (!booted) { // primeira volta do loop: a abertura fica so enquanto a rede (Wi-Fi do C6) inicia
    booted = true;
    // Ja configurado (Wi-Fi salvo ou vinculado pelo cabo): direto para a leitura. Wi-Fi salvo mas sem
    // vinculo: passo 2 do assistente. Nada configurado: boas-vindas.
    if (cloud_is_paired()) { build_home(); show(scr_home); }
    else if (has_wifi_credentials()) open_pair();
    else open_welcome();
    scr_splash = NULL; // apagada por show()
  }
}

#endif // BREWW_HAS_LVGL
