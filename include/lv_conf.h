/*
 * Minimal LVGL 8.x config. Only overrides that matter for this project are set here;
 * lv_conf_internal.h fills in sensible defaults for everything else.
 */
#ifndef LV_CONF_H
#define LV_CONF_H

#if 1 /* Set to 1 to enable content, required for LVGL to read this file */

#include <stdint.h>

/* Color depth: 16 (RGB565) matches the ILI9341 panel */
#define LV_COLOR_DEPTH 16

/* Byte order matches TFT_eSPI's own pushColors(..., swap=true) call in the flush callback,
 * so leave this at the LVGL default (no extra swap inside LVGL itself). */
#define LV_COLOR_16_SWAP 0

/* We drive the LVGL tick manually from loop(), not from a HW timer/ISR */
#define LV_TICK_CUSTOM 0

/* Default working-set heap for LVGL's internal allocator (widgets, styles, etc.) */
#define LV_MEM_SIZE (32U * 1024U)

/* Larger fonts for title text / warning banner; 14 (default) stays on for body text */
#define LV_FONT_MONTSERRAT_14 1
#define LV_FONT_MONTSERRAT_16 1
#define LV_FONT_MONTSERRAT_20 1
#define LV_FONT_MONTSERRAT_24 1

#define LV_USE_TABLE 1

#endif /* End of content */

#endif /* LV_CONF_H */
