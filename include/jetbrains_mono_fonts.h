#pragma once

// Custom LVGL bitmap fonts converted from JetBrains Mono Regular (OFL-1.1) via lv_font_conv.
// Source .c files live in src/fonts/; regenerate with lv_font_conv if a size/range changes.

#include <lvgl.h>

extern const lv_font_t jetbrains_mono_14;
extern const lv_font_t jetbrains_mono_16;
extern const lv_font_t jetbrains_mono_20;
extern const lv_font_t jetbrains_mono_24;

// Play/pause/circle glyphs from Font Awesome 6 Free Solid (OFL-1.1), at the same codepoints
// as LV_SYMBOL_PLAY/LV_SYMBOL_PAUSE - used for the table view's Status column.
extern const lv_font_t status_icons_14;
#define STATUS_ICON_BUFFERING "\xEF\x84\x91" // U+F111, solid circle
