#if 1
#ifndef LV_CONF_H
#define LV_CONF_H

#include <stdint.h>
#define LV_COLOR_DEPTH 16
#define LV_COLOR_16_SWAP 0
#define LV_MEM_CUSTOM 0
#define LV_MEM_SIZE (128U * 1024U)
/* Placas com PSRAM: o bloco de memoria do LVGL (objetos/estilos) vai para a PSRAM,
 * liberando ~128 KB de RAM interna para Wi-Fi/TLS/AES (o buffer de desenho continua interno). */
#if defined(BOARD_HAS_PSRAM)
#define LV_MEM_POOL_INCLUDE <esp32-hal-psram.h>
#define LV_MEM_POOL_ALLOC ps_malloc
#endif
#define LV_TICK_CUSTOM 1
#define LV_TICK_CUSTOM_INCLUDE "Arduino.h"
#define LV_TICK_CUSTOM_SYS_TIME_EXPR (millis())
#define LV_USE_LOG 0
#define LV_FONT_MONTSERRAT_10 1
#define LV_FONT_MONTSERRAT_12 1
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_24 1
#define LV_FONT_MONTSERRAT_32 1
#define LV_FONT_MONTSERRAT_48 1
#define LV_USE_THEME_DEFAULT 1
#define LV_THEME_DEFAULT_DARK 1
#define LV_USE_QRCODE 1 /* QR code de pareamento (breww_controller) */

#endif
#endif
