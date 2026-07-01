#include <Arduino.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <lvgl.h>
#include <time.h>

#include "network_task.h"
#include "ui.h"

// Touch presses aren't registering on this unit at all (confirmed via a raw-read diagnostic
// build) - disabled until the touch controller mismatch is resolved, so setup() doesn't hang
// forever in tft.calibrateTouch() waiting for a press that never comes.
#define ENABLE_TOUCH 0

static const uint32_t SCREEN_W = 320;
static const uint32_t SCREEN_H = 240;

static TFT_eSPI tft;

static lv_disp_draw_buf_t drawBuf;
static lv_color_t lvBuf1[SCREEN_W * 20]; // partial buffer (20 rows); no PSRAM needed

static void dispFlush(lv_disp_drv_t* drv, const lv_area_t* area, lv_color_t* color_p) {
    uint32_t w = area->x2 - area->x1 + 1;
    uint32_t h = area->y2 - area->y1 + 1;

    tft.startWrite();
    tft.setAddrWindow(area->x1, area->y1, w, h);
    tft.pushColors((uint16_t*)&color_p->full, w * h, true);
    tft.endWrite();

    lv_disp_flush_ready(drv);
}

static void touchpadRead(lv_indev_drv_t* indev, lv_indev_data_t* data) {
    uint16_t tx, ty;
    if (tft.getTouch(&tx, &ty)) {
        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = tx;
        data->point.y = ty;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// Resistive touch needs per-unit calibration (the raw ADC range varies panel to panel).
// Calibrate once and persist the result to flash, so this only runs on the very first boot.
static void initTouch() {
    Preferences prefs;
    prefs.begin("touchcal", false);

    uint16_t calData[5];
    size_t len = prefs.getBytes("caldata", calData, sizeof(calData));
    if (len == sizeof(calData)) {
        tft.setTouch(calData);
        Serial.println("[touch] loaded saved calibration");
    } else {
        Serial.println("[touch] no calibration saved - follow the on-screen prompts, tap each crosshair");
        tft.calibrateTouch(calData, TFT_WHITE, TFT_RED, 15);
        prefs.putBytes("caldata", calData, sizeof(calData));
        Serial.println("[touch] calibration complete and saved");
    }

    prefs.end();
}

void setup() {
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1); // landscape, 320x240
    tft.fillScreen(TFT_BLACK);
#if ENABLE_TOUCH
    initTouch();
    tft.fillScreen(TFT_BLACK); // calibrateTouch leaves its crosshairs on screen otherwise
#endif

    lv_init();
    lv_disp_draw_buf_init(&drawBuf, lvBuf1, nullptr, SCREEN_W * 20);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = SCREEN_W;
    dispDrv.ver_res = SCREEN_H;
    dispDrv.flush_cb = dispFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

#if ENABLE_TOUCH
    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = touchpadRead;
    lv_indev_drv_register(&indevDrv);
#endif

    ui_init();
    networkTaskStart();
}

static void updateClock() {
    static unsigned long lastUpdateMs = 0;
    unsigned long now = millis();
    if (now - lastUpdateMs < 1000) return;
    lastUpdateMs = now;

    time_t nowSec = time(nullptr);
    char buf[12];
    if (nowSec > 1600000000) { // NTP has synced at least once (see network_task.cpp)
        struct tm timeinfo;
        localtime_r(&nowSec, &timeinfo);
        int hour12 = timeinfo.tm_hour % 12;
        if (hour12 == 0) hour12 = 12;
        const char* ampm = (timeinfo.tm_hour < 12) ? "am" : "pm";
        snprintf(buf, sizeof(buf), "%d:%02d%s", hour12, timeinfo.tm_min, ampm);
    } else {
        strlcpy(buf, "--:--", sizeof(buf));
    }
    ui_set_clock(buf);
}

void loop() {
    lv_timer_handler();
    lv_tick_inc(5);
    updateClock();

    SharedState state;
    if (networkTaskPollUpdate(&state)) {
        const uint16_t* art = state.artValid ? state.artBuffer : nullptr;
        ui_update(state.mode, state.sessions, state.sessionCount, art, state.artWidth,
                  state.artHeight);
        // TFT_BACKLIGHT_ON reflects the "on" level from platformio.ini; invert it when the
        // screensaver (idle timeout or night-mode hours) should keep the display dark.
        digitalWrite(TFT_BL, state.screensaverActive ? !TFT_BACKLIGHT_ON : TFT_BACKLIGHT_ON);
    }

    delay(5);
}
