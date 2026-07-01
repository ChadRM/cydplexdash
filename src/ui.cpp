#include "ui.h"

#include <lvgl.h>

static lv_obj_t* s_errorView;
static lv_obj_t* s_idleView;
static lv_obj_t* s_singleView;
static lv_obj_t* s_tableView;

static lv_obj_t* s_artImg;
static lv_obj_t* s_userLabel;
static lv_obj_t* s_titleLabel;
static lv_obj_t* s_subtitleLabel;
static lv_obj_t* s_stateLabel;
static lv_obj_t* s_progressBar;
static lv_obj_t* s_timeLabel;

static lv_img_dsc_t s_artDsc;

static const char* stateToString(PlayState state) {
    switch (state) {
        case PlayState::PAUSED: return "Paused";
        case PlayState::BUFFERING: return "Buffering";
        case PlayState::PLAYING: return "Playing";
        default: return "";
    }
}

static void formatTime(uint32_t ms, char* out, size_t outSize) {
    uint32_t totalSec = ms / 1000;
    uint32_t m = totalSec / 60;
    uint32_t s = totalSec % 60;
    snprintf(out, outSize, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
}

static void hideAllViews() {
    lv_obj_add_flag(s_errorView, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_idleView, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_singleView, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_tableView, LV_OBJ_FLAG_HIDDEN);
}

void ui_init() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    // --- Error view ---
    s_errorView = lv_obj_create(scr);
    lv_obj_set_size(s_errorView, 320, 240);
    lv_obj_set_pos(s_errorView, 0, 0);
    lv_obj_set_style_bg_color(s_errorView, lv_palette_main(LV_PALETTE_RED), 0);
    lv_obj_set_style_border_width(s_errorView, 0, 0);
    lv_obj_set_style_radius(s_errorView, 0, 0);
    lv_obj_clear_flag(s_errorView, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* errLabel = lv_label_create(s_errorView);
    lv_label_set_text(errLabel, "Plex Server Unreachable");
    lv_obj_set_style_text_font(errLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(errLabel, lv_color_white(), 0);
    lv_obj_set_width(errLabel, 280);
    lv_label_set_long_mode(errLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(errLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(errLabel);

    // --- Idle view ---
    s_idleView = lv_obj_create(scr);
    lv_obj_set_size(s_idleView, 320, 240);
    lv_obj_set_pos(s_idleView, 0, 0);
    lv_obj_set_style_bg_color(s_idleView, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_idleView, 0, 0);
    lv_obj_set_style_radius(s_idleView, 0, 0);
    lv_obj_clear_flag(s_idleView, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* idleLabel = lv_label_create(s_idleView);
    lv_label_set_text(idleLabel, "Nothing Playing");
    lv_obj_set_style_text_font(idleLabel, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(idleLabel, lv_color_white(), 0);
    lv_obj_center(idleLabel);

    // --- Single-session view (full/centered art + overlay text panel) ---
    s_singleView = lv_obj_create(scr);
    lv_obj_set_size(s_singleView, 320, 240);
    lv_obj_set_pos(s_singleView, 0, 0);
    lv_obj_set_style_bg_color(s_singleView, lv_color_black(), 0);
    lv_obj_set_style_border_width(s_singleView, 0, 0);
    lv_obj_set_style_radius(s_singleView, 0, 0);
    lv_obj_set_style_pad_all(s_singleView, 0, 0);
    lv_obj_clear_flag(s_singleView, LV_OBJ_FLAG_SCROLLABLE);

    s_artImg = lv_img_create(s_singleView);

    lv_obj_t* overlay = lv_obj_create(s_singleView);
    lv_obj_set_size(overlay, 320, 90);
    lv_obj_set_pos(overlay, 0, 150);
    lv_obj_set_style_bg_color(overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 6, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);

    s_userLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_userLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_userLabel, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_pos(s_userLabel, 0, 0);

    s_titleLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_titleLabel, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(s_titleLabel, lv_color_white(), 0);
    lv_label_set_long_mode(s_titleLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_titleLabel, 300);
    lv_obj_set_pos(s_titleLabel, 0, 16);

    s_subtitleLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_subtitleLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_subtitleLabel, lv_color_white(), 0);
    lv_label_set_long_mode(s_subtitleLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_subtitleLabel, 300);
    lv_obj_set_pos(s_subtitleLabel, 0, 40);

    s_stateLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_stateLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_stateLabel, lv_palette_main(LV_PALETTE_GREEN), 0);
    lv_obj_set_pos(s_stateLabel, 0, 58);

    s_progressBar = lv_bar_create(overlay);
    lv_obj_set_size(s_progressBar, 220, 8);
    lv_obj_set_pos(s_progressBar, 0, 76);
    lv_bar_set_range(s_progressBar, 0, 100);

    s_timeLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_timeLabel, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_timeLabel, lv_color_white(), 0);
    lv_obj_set_pos(s_timeLabel, 230, 74);

    // --- Table view (2+ concurrent sessions) ---
    s_tableView = lv_table_create(scr);
    lv_obj_set_size(s_tableView, 320, 240);
    lv_obj_set_pos(s_tableView, 0, 0);
    lv_table_set_col_cnt(s_tableView, 3);
    lv_table_set_col_width(s_tableView, 0, 70);
    lv_table_set_col_width(s_tableView, 1, 170);
    lv_table_set_col_width(s_tableView, 2, 80);
    lv_obj_set_style_text_font(s_tableView, &lv_font_montserrat_14, 0);

    hideAllViews();
}

static void updateSingleView(const Session& s, const uint16_t* artBuffer, int artW, int artH) {
    lv_label_set_text(s_userLabel, s.username);
    lv_label_set_text(s_titleLabel, s.title);
    lv_label_set_text(s_subtitleLabel, s.subtitle);
    lv_label_set_text(s_stateLabel, stateToString(s.state));

    if (artBuffer) {
        s_artDsc.header.always_zero = 0;
        s_artDsc.header.w = artW;
        s_artDsc.header.h = artH;
        s_artDsc.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_artDsc.data_size = (uint32_t)artW * artH * sizeof(uint16_t);
        s_artDsc.data = (const uint8_t*)artBuffer;
        lv_img_set_src(s_artImg, &s_artDsc);
        lv_obj_set_pos(s_artImg, (320 - artW) / 2, (240 - artH) / 2);
    }

    if (s.durationMs > 0) {
        int pct = (int)((uint64_t)s.progressMs * 100 / s.durationMs);
        lv_bar_set_value(s_progressBar, pct, LV_ANIM_OFF);
    } else {
        lv_bar_set_value(s_progressBar, 0, LV_ANIM_OFF);
    }

    char elapsed[16];
    char total[16];
    formatTime(s.progressMs, elapsed, sizeof(elapsed));
    formatTime(s.durationMs, total, sizeof(total));
    char timeBuf[40];
    snprintf(timeBuf, sizeof(timeBuf), "%s / %s", elapsed, total);
    lv_label_set_text(s_timeLabel, timeBuf);
}

static void updateTableView(const Session* sessions, int count) {
    lv_table_set_row_cnt(s_tableView, count + 1);
    lv_table_set_cell_value(s_tableView, 0, 0, "User");
    lv_table_set_cell_value(s_tableView, 0, 1, "Watching");
    lv_table_set_cell_value(s_tableView, 0, 2, "Status");

    for (int i = 0; i < count; i++) {
        const Session& s = sessions[i];
        lv_table_set_cell_value(s_tableView, i + 1, 0, s.username);

        char combined[110];
        if (s.subtitle[0]) {
            snprintf(combined, sizeof(combined), "%s (%s)", s.title, s.subtitle);
        } else {
            snprintf(combined, sizeof(combined), "%s", s.title);
        }
        lv_table_set_cell_value(s_tableView, i + 1, 1, combined);
        lv_table_set_cell_value(s_tableView, i + 1, 2, stateToString(s.state));
    }
}

void ui_update(DisplayMode mode, const Session* sessions, int count, const uint16_t* artBuffer,
               int artWidth, int artHeight) {
    hideAllViews();

    switch (mode) {
        case DisplayMode::ERROR_SCREEN:
            lv_obj_clear_flag(s_errorView, LV_OBJ_FLAG_HIDDEN);
            break;
        case DisplayMode::IDLE:
            lv_obj_clear_flag(s_idleView, LV_OBJ_FLAG_HIDDEN);
            break;
        case DisplayMode::SINGLE:
            lv_obj_clear_flag(s_singleView, LV_OBJ_FLAG_HIDDEN);
            if (count > 0) updateSingleView(sessions[0], artBuffer, artWidth, artHeight);
            break;
        case DisplayMode::TABLE:
            lv_obj_clear_flag(s_tableView, LV_OBJ_FLAG_HIDDEN);
            updateTableView(sessions, count);
            break;
    }
}
