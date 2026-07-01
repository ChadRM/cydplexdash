#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen_TT.h>
#include <lvgl.h>
#include <time.h>

#include "network_task.h"
#include "ui.h"

static const uint32_t SCREEN_W = 320;
static const uint32_t SCREEN_H = 240;

static TFT_eSPI tft;

// Touch (XPT2046) is wired to its own dedicated SPI bus on this board, separate from the
// display's - confirmed from the seller's reference firmware. Using a second SPIClass bound
// to HSPI (the display uses the default VSPI-backed `SPI` object via TFT_eSPI) so both buses
// run independently with no pin conflicts.
static const int TOUCH_CS_PIN = 33;
static const int TOUCH_IRQ_PIN = 36;
static const int TOUCH_SCK_PIN = 25;
static const int TOUCH_MISO_PIN = 39;
static const int TOUCH_MOSI_PIN = 32;

static SPIClass touchSPI(HSPI);
static XPT2046_Touchscreen ts(TOUCH_CS_PIN, TOUCH_IRQ_PIN);

// Raw ADC bounds for mapping touch coordinates to screen pixels, measured from this unit's
// actual corner presses (observed range: x 289-3639, y 442-3777), with a small margin.
// If dragging ever feels reversed on an axis, swap that axis's min/max below.
static const int16_t TOUCH_RAW_X_MIN = 250;
static const int16_t TOUCH_RAW_X_MAX = 3700;
static const int16_t TOUCH_RAW_Y_MIN = 400;
static const int16_t TOUCH_RAW_Y_MAX = 3820;

static lv_disp_draw_buf_t drawBuf;
static lv_color_t lvBuf1[SCREEN_W * 20]; // partial buffer (20 rows); no PSRAM needed

// Wake-on-tap: a touch while the screen is dark (idle-timeout or night-mode) forces the
// backlight on for 5 minutes, independent of the underlying idle/night condition.
static const unsigned long WAKE_WINDOW_MS = 5UL * 60UL * 1000UL;
static bool g_lastScreensaverActive = false;
static unsigned long g_wakeUntilMs = 0;

static bool isEffectivelyDark() {
    return g_lastScreensaverActive && (millis() >= g_wakeUntilMs);
}

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
    if (!ts.touched()) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    if (isEffectivelyDark()) {
        g_wakeUntilMs = millis() + WAKE_WINDOW_MS;
        Serial.println("[touch] wake tap - backlight on for 5 minutes");
    }

    TS_Point p = ts.getPoint();

    static unsigned long lastLogMs = 0;
    if (millis() - lastLogMs > 300) {
        Serial.printf("[touch] raw x=%d y=%d z=%d\n", p.x, p.y, p.z);
        lastLogMs = millis();
    }

    int32_t px = map(p.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, SCREEN_W - 1);
    int32_t py = map(p.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, SCREEN_H - 1);
    px = constrain(px, 0, (int32_t)SCREEN_W - 1);
    py = constrain(py, 0, (int32_t)SCREEN_H - 1);

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = px;
    data->point.y = py;
}

void setup() {
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1); // landscape, 320x240
    tft.fillScreen(TFT_BLACK);

    touchSPI.begin(TOUCH_SCK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
    ts.begin(touchSPI);
    ts.setRotation(1); // match the display's rotation

    lv_init();
    lv_disp_draw_buf_init(&drawBuf, lvBuf1, nullptr, SCREEN_W * 20);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = SCREEN_W;
    dispDrv.ver_res = SCREEN_H;
    dispDrv.flush_cb = dispFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    static lv_indev_drv_t indevDrv;
    lv_indev_drv_init(&indevDrv);
    indevDrv.type = LV_INDEV_TYPE_POINTER;
    indevDrv.read_cb = touchpadRead;
    lv_indev_drv_register(&indevDrv);

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
                  state.artHeight, state.recentViews, state.recentViewCount);
        g_lastScreensaverActive = state.screensaverActive;
    }

    // TFT_BACKLIGHT_ON reflects the "on" level from platformio.ini; invert it when the screen
    // should be dark (idle-timeout or night-mode), unless a wake-on-tap window is active.
    digitalWrite(TFT_BL, isEffectivelyDark() ? !TFT_BACKLIGHT_ON : TFT_BACKLIGHT_ON);

    delay(5);
}
