#include <Arduino.h>
#include <TFT_eSPI.h>
#include <lvgl.h>

#include "network_task.h"
#include "ui.h"

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

void setup() {
    Serial.begin(115200);

    tft.init();
    tft.setRotation(1); // landscape, 320x240
    tft.fillScreen(TFT_BLACK);

    lv_init();
    lv_disp_draw_buf_init(&drawBuf, lvBuf1, nullptr, SCREEN_W * 20);

    static lv_disp_drv_t dispDrv;
    lv_disp_drv_init(&dispDrv);
    dispDrv.hor_res = SCREEN_W;
    dispDrv.ver_res = SCREEN_H;
    dispDrv.flush_cb = dispFlush;
    dispDrv.draw_buf = &drawBuf;
    lv_disp_drv_register(&dispDrv);

    ui_init();
    networkTaskStart();
}

void loop() {
    lv_timer_handler();
    lv_tick_inc(5);

    SharedState state;
    if (networkTaskPollUpdate(&state)) {
        const uint16_t* art = state.artValid ? state.artBuffer : nullptr;
        ui_update(state.mode, state.sessions, state.sessionCount, art, state.artWidth,
                  state.artHeight);
    }

    delay(5);
}
