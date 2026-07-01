#include "ui.h"

#include <lvgl.h>

#include "jetbrains_mono_fonts.h"
#include "secrets.h"

static const int TOP_BAR_H = 22;

// Tokyo Night palette (https://github.com/enkia/tokyo-night-vscode-theme)
static const uint32_t COLOR_BG = 0x1a1b26;           // main background
static const uint32_t COLOR_BG_DARK = 0x16161e;      // recessed panels (top bar, table)
static const uint32_t COLOR_BG_HIGHLIGHT = 0x292e42; // elevated surfaces (overlay, table header)
static const uint32_t COLOR_BORDER = 0x3b4261;
static const uint32_t COLOR_FG = 0xc0caf5;      // primary text
static const uint32_t COLOR_FG_DIM = 0xa9b1d6;  // secondary text
static const uint32_t COLOR_COMMENT = 0x565f89; // de-emphasized text
static const uint32_t COLOR_BLUE = 0x7aa2f7;
static const uint32_t COLOR_PURPLE = 0xbb9af7;
static const uint32_t COLOR_GREEN = 0x9ece6a;
static const uint32_t COLOR_ORANGE = 0xff9e64;
static const uint32_t COLOR_YELLOW = 0xe0af68;
static const uint32_t COLOR_RED = 0xf7768e;

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
static lv_obj_t* s_clockLabel;
static lv_obj_t* s_recentHeaderLabel;
static lv_obj_t* s_recentLabel[MAX_RECENT_VIEWS];

static lv_img_dsc_t s_artDsc;

static const char* stateToString(PlayState state) {
    switch (state) {
        case PlayState::PAUSED: return "Paused";
        case PlayState::BUFFERING: return "Buffering";
        case PlayState::PLAYING: return "Playing";
        default: return "";
    }
}

// Icon-glyph form of stateToString(), for the table view's narrow Status column.
static const char* stateToIcon(PlayState state) {
    switch (state) {
        case PlayState::PAUSED: return LV_SYMBOL_PAUSE;
        case PlayState::BUFFERING: return STATUS_ICON_BUFFERING;
        case PlayState::PLAYING: return LV_SYMBOL_PLAY;
        default: return "";
    }
}

static lv_color_t stateColor(PlayState state) {
    switch (state) {
        case PlayState::PLAYING: return lv_color_hex(COLOR_GREEN);
        case PlayState::PAUSED: return lv_color_hex(COLOR_YELLOW);
        case PlayState::BUFFERING: return lv_color_hex(COLOR_BLUE);
        default: return lv_color_hex(COLOR_FG_DIM);
    }
}

// Mirrors the table's current Status column per row, so the draw-part callback can color
// each row by playback state without needing access to the full Session data there.
static PlayState s_tableRowState[MAX_SESSIONS];

static void formatTime(uint32_t ms, char* out, size_t outSize) {
    uint32_t totalSec = ms / 1000;
    uint32_t m = totalSec / 60;
    uint32_t s = totalSec % 60;
    snprintf(out, outSize, "%lu:%02lu", (unsigned long)m, (unsigned long)s);
}

// Gives the table a dark theme (LVGL's default table style is light-mode), highlights the
// header row (row 0) with a lighter background + accent text, and colors each data row's
// Status cell by playback state (green/yellow/blue), matching the single-session view.
static void tableDrawPartEventCb(lv_event_t* e) {
    lv_obj_t* obj = lv_event_get_target(e);
    lv_obj_draw_part_dsc_t* dsc = lv_event_get_draw_part_dsc(e);
    if (dsc->part != LV_PART_ITEMS) return;

    uint32_t colCnt = lv_table_get_col_cnt(obj);
    uint32_t row = dsc->id / colCnt;
    uint32_t col = dsc->id % colCnt;

    if (row == 0) {
        dsc->rect_dsc->bg_color = lv_color_hex(COLOR_BG_HIGHLIGHT);
        dsc->label_dsc->color = lv_color_hex(COLOR_PURPLE);
    } else if (col == 2 && (row - 1) < MAX_SESSIONS) {
        dsc->label_dsc->color = stateColor(s_tableRowState[row - 1]);
        dsc->label_dsc->font = &status_icons_14;
        dsc->label_dsc->align = LV_TEXT_ALIGN_CENTER;
    }
}

static void hideAllViews() {
    lv_obj_add_flag(s_errorView, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_idleView, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_singleView, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_tableView, LV_OBJ_FLAG_HIDDEN);
}

void ui_init() {
    lv_obj_t* scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_hex(COLOR_BG), 0);

    // --- Error view ---
    s_errorView = lv_obj_create(scr);
    lv_obj_set_size(s_errorView, 320, 240);
    lv_obj_set_pos(s_errorView, 0, 0);
    lv_obj_set_style_bg_color(s_errorView, lv_color_hex(COLOR_RED), 0);
    lv_obj_set_style_border_width(s_errorView, 0, 0);
    lv_obj_set_style_radius(s_errorView, 0, 0);
    lv_obj_clear_flag(s_errorView, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* errLabel = lv_label_create(s_errorView);
    lv_label_set_text(errLabel, "Plex Server Unreachable");
    lv_obj_set_style_text_font(errLabel, &jetbrains_mono_24, 0);
    lv_obj_set_style_text_color(errLabel, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_width(errLabel, 280);
    lv_label_set_long_mode(errLabel, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(errLabel, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(errLabel);

    // --- Idle view ---
    s_idleView = lv_obj_create(scr);
    lv_obj_set_size(s_idleView, 320, 240);
    lv_obj_set_pos(s_idleView, 0, 0);
    lv_obj_set_style_bg_color(s_idleView, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(s_idleView, 0, 0);
    lv_obj_set_style_radius(s_idleView, 0, 0);
    lv_obj_clear_flag(s_idleView, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* idleLabel = lv_label_create(s_idleView);
    lv_label_set_text(idleLabel, "Nothing Playing Now");
    lv_obj_set_style_text_font(idleLabel, &jetbrains_mono_24, 0);
    lv_obj_set_style_text_color(idleLabel, lv_color_hex(COLOR_PURPLE), 0);
    lv_obj_align(idleLabel, LV_ALIGN_TOP_MID, 0, 50);

    s_recentHeaderLabel = lv_label_create(s_idleView);
    lv_obj_set_style_text_font(s_recentHeaderLabel, &jetbrains_mono_14, 0);
    lv_obj_set_style_text_color(s_recentHeaderLabel, lv_color_hex(COLOR_COMMENT), 0);
    lv_obj_align(s_recentHeaderLabel, LV_ALIGN_TOP_MID, 0, 110);

    static const uint32_t recentColors[MAX_RECENT_VIEWS] = {COLOR_BLUE, COLOR_ORANGE};
    for (int i = 0; i < MAX_RECENT_VIEWS; i++) {
        s_recentLabel[i] = lv_label_create(s_idleView);
        lv_obj_set_style_text_font(s_recentLabel[i], &jetbrains_mono_16, 0);
        lv_obj_set_style_text_color(s_recentLabel[i],
                                     lv_color_hex(recentColors[i % MAX_RECENT_VIEWS]), 0);
        lv_label_set_long_mode(s_recentLabel[i], LV_LABEL_LONG_DOT);
        lv_obj_set_width(s_recentLabel[i], 300);
        lv_obj_set_style_text_align(s_recentLabel[i], LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_align(s_recentLabel[i], LV_ALIGN_TOP_MID, 0, 136 + i * 26);
    }

    // --- Single-session view (full/centered art + overlay text panel) ---
    s_singleView = lv_obj_create(scr);
    lv_obj_set_size(s_singleView, 320, 240);
    lv_obj_set_pos(s_singleView, 0, 0);
    lv_obj_set_style_bg_color(s_singleView, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_border_width(s_singleView, 0, 0);
    lv_obj_set_style_radius(s_singleView, 0, 0);
    lv_obj_set_style_pad_all(s_singleView, 0, 0);
    lv_obj_clear_flag(s_singleView, LV_OBJ_FLAG_SCROLLABLE);

    s_artImg = lv_img_create(s_singleView);

    lv_obj_t* overlay = lv_obj_create(s_singleView);
    lv_obj_set_size(overlay, 320, 90);
    lv_obj_set_pos(overlay, 0, 150);
    lv_obj_set_style_bg_color(overlay, lv_color_hex(COLOR_BG_HIGHLIGHT), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(overlay, 0, 0);
    lv_obj_set_style_radius(overlay, 0, 0);
    lv_obj_set_style_pad_all(overlay, 4, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE);
    // Note: child positions below are relative to this content area (already inset by the
    // 4px padding above, on all 4 sides) - this box is flush with the screen edge, so
    // anything past its content height (90 - 2*4 = 82px) gets clipped off-screen.

    s_userLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_userLabel, &jetbrains_mono_14, 0);
    lv_obj_set_style_text_color(s_userLabel, lv_color_hex(COLOR_ORANGE), 0);
    lv_obj_set_pos(s_userLabel, 0, 0);

    s_titleLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_titleLabel, &jetbrains_mono_20, 0);
    lv_obj_set_style_text_color(s_titleLabel, lv_color_hex(COLOR_FG), 0);
    lv_label_set_long_mode(s_titleLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_titleLabel, 300);
    lv_obj_set_pos(s_titleLabel, 0, 14);

    s_subtitleLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_subtitleLabel, &jetbrains_mono_14, 0);
    lv_obj_set_style_text_color(s_subtitleLabel, lv_color_hex(COLOR_FG_DIM), 0);
    lv_label_set_long_mode(s_subtitleLabel, LV_LABEL_LONG_DOT);
    lv_obj_set_width(s_subtitleLabel, 300);
    lv_obj_set_pos(s_subtitleLabel, 0, 34);

    s_stateLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_stateLabel, &jetbrains_mono_14, 0);
    lv_obj_set_style_text_color(s_stateLabel, lv_color_hex(COLOR_GREEN), 0);
    lv_obj_set_pos(s_stateLabel, 0, 50);

    s_progressBar = lv_bar_create(overlay);
    lv_obj_set_size(s_progressBar, 170, 8);
    lv_obj_set_pos(s_progressBar, 0, 64);
    lv_bar_set_range(s_progressBar, 0, 100);
    lv_obj_set_style_bg_color(s_progressBar, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progressBar, lv_color_hex(COLOR_BLUE), LV_PART_INDICATOR);

    // JetBrains Mono is noticeably wider per character than the old Montserrat labels were,
    // so this is right-aligned within a fixed box (instead of a bare x position) - it can't
    // overflow past the content edge regardless of exact digit count ("1:23" vs "12:34").
    s_timeLabel = lv_label_create(overlay);
    lv_obj_set_style_text_font(s_timeLabel, &jetbrains_mono_14, 0);
    lv_obj_set_style_text_color(s_timeLabel, lv_color_hex(COLOR_FG_DIM), 0);
    lv_obj_set_pos(s_timeLabel, 178, 62);
    lv_obj_set_width(s_timeLabel, 134);
    lv_obj_set_style_text_align(s_timeLabel, LV_TEXT_ALIGN_RIGHT, 0);

    // --- Table view (2+ concurrent sessions) ---
    s_tableView = lv_table_create(scr);
    lv_obj_set_size(s_tableView, 320, 240 - TOP_BAR_H);
    lv_obj_set_pos(s_tableView, 0, TOP_BAR_H);
    lv_table_set_col_cnt(s_tableView, 3);
    lv_table_set_col_width(s_tableView, 0, 70);
    lv_table_set_col_width(s_tableView, 1, 216);
    lv_table_set_col_width(s_tableView, 2, 34);
    lv_obj_set_style_text_font(s_tableView, &jetbrains_mono_14, 0);

    // Dark theme: LVGL's default table style is light-mode (white cells/black text/gray border).
    lv_obj_set_style_bg_color(s_tableView, lv_color_hex(COLOR_BG_DARK), LV_PART_MAIN);
    lv_obj_set_style_border_color(s_tableView, lv_color_hex(COLOR_BORDER), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_tableView, lv_color_hex(COLOR_BG), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_tableView, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_tableView, lv_color_hex(COLOR_FG), LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_tableView, lv_color_hex(COLOR_BORDER), LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_tableView, 1, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_tableView, tableDrawPartEventCb, LV_EVENT_DRAW_PART_BEGIN, nullptr);

    // --- Persistent server-name bar, shown above every view ---
    lv_obj_t* topBar = lv_obj_create(scr);
    lv_obj_set_size(topBar, 320, TOP_BAR_H);
    lv_obj_set_pos(topBar, 0, 0);
    lv_obj_set_style_bg_color(topBar, lv_color_hex(COLOR_BG_DARK), 0);
    lv_obj_set_style_bg_opa(topBar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(topBar, 0, 0);
    lv_obj_set_style_radius(topBar, 0, 0);
    lv_obj_set_style_pad_all(topBar, 2, 0);
    lv_obj_clear_flag(topBar, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* serverLabel = lv_label_create(topBar);
    lv_label_set_text(serverLabel, PLEX_SERVER_NAME);
    lv_obj_set_style_text_font(serverLabel, &jetbrains_mono_14, 0);
    lv_obj_set_style_text_color(serverLabel, lv_color_hex(COLOR_FG), 0);
    lv_obj_align(serverLabel, LV_ALIGN_LEFT_MID, 2, 0);

    s_clockLabel = lv_label_create(topBar);
    lv_label_set_text(s_clockLabel, "--:--");
    lv_obj_set_style_text_font(s_clockLabel, &jetbrains_mono_14, 0);
    lv_obj_set_style_text_color(s_clockLabel, lv_color_hex(COLOR_BLUE), 0);
    lv_obj_align(s_clockLabel, LV_ALIGN_RIGHT_MID, -2, 0);

    lv_obj_move_foreground(topBar); // always on top, regardless of which view is active

    hideAllViews();
}

static void updateSingleView(const Session& s, const uint16_t* artBuffer, int artW, int artH) {
    lv_label_set_text(s_userLabel, s.username);
    lv_label_set_text(s_titleLabel, s.title);
    lv_label_set_text(s_subtitleLabel, s.subtitle);
    lv_label_set_text(s_stateLabel, stateToString(s.state));
    lv_obj_set_style_text_color(s_stateLabel, stateColor(s.state), 0);

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

static void updateIdleView(const RecentView* recentViews, int recentViewCount) {
    lv_label_set_text(s_recentHeaderLabel, recentViewCount > 0 ? "Recently watched:" : "");

    for (int i = 0; i < MAX_RECENT_VIEWS; i++) {
        if (i >= recentViewCount) {
            lv_label_set_text(s_recentLabel[i], "");
            continue;
        }
        const RecentView& v = recentViews[i];
        char combined[140];
        if (v.subtitle[0]) {
            snprintf(combined, sizeof(combined), "%s: %s (%s)", v.username, v.title, v.subtitle);
        } else {
            snprintf(combined, sizeof(combined), "%s: %s", v.username, v.title);
        }
        lv_label_set_text(s_recentLabel[i], combined);
    }
}

static void updateTableView(const Session* sessions, int count) {
    lv_table_set_row_cnt(s_tableView, count + 1);
    lv_table_set_cell_value(s_tableView, 0, 0, "User");
    lv_table_set_cell_value(s_tableView, 0, 1, "Watching");
    lv_table_set_cell_value(s_tableView, 0, 2, "");

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
        lv_table_set_cell_value(s_tableView, i + 1, 2, stateToIcon(s.state));
        s_tableRowState[i] = s.state;
    }
}

void ui_update(DisplayMode mode, const Session* sessions, int count, const uint16_t* artBuffer,
               int artWidth, int artHeight, const RecentView* recentViews, int recentViewCount) {
    hideAllViews();

    switch (mode) {
        case DisplayMode::ERROR_SCREEN:
            lv_obj_clear_flag(s_errorView, LV_OBJ_FLAG_HIDDEN);
            break;
        case DisplayMode::IDLE:
            lv_obj_clear_flag(s_idleView, LV_OBJ_FLAG_HIDDEN);
            updateIdleView(recentViews, recentViewCount);
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

void ui_set_clock(const char* text) {
    lv_label_set_text(s_clockLabel, text);
    lv_obj_align(s_clockLabel, LV_ALIGN_RIGHT_MID, -2, 0);
}
