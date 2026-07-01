#include "network_task.h"

#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <esp_heap_caps.h>

#include "secrets.h"

static SemaphoreHandle_t g_mutex;
static SharedState g_state = {};
static volatile bool g_dirty = false;

static uint16_t* g_artBuffer = nullptr;
static int g_artBufW = 0;
static int g_artBufH = 0;
static char g_lastThumbPath[128] = "";

// Set only while a decode is in flight; the TJpg_Decoder callback writes through it.
static uint16_t* g_decodeTarget = nullptr;
static int g_decodeW = 0;
static int g_decodeH = 0;

static bool jpegOutputCallback(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
    if (!g_decodeTarget) return false;
    for (int row = 0; row < h; row++) {
        int py = y + row;
        if (py < 0 || py >= g_decodeH || x >= g_decodeW) continue;
        int copyW = w;
        if (x + copyW > g_decodeW) copyW = g_decodeW - x;
        if (copyW <= 0) continue;
        memcpy(&g_decodeTarget[py * g_decodeW + x], &bitmap[row * w], copyW * sizeof(uint16_t));
    }
    return true;
}

static bool fetchAndDecodeArt(const char* thumbPath) {
    String url = buildArtUrl(thumbPath, g_artBufW, g_artBufH);

    HTTPClient http;
    http.begin(url);
    http.setConnectTimeout(4000);
    http.setTimeout(4000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    int len = http.getSize();
    if (len <= 0 || len > 150000) { // sanity cap; a 320x240 JPEG thumbnail is well under this
        http.end();
        return false;
    }

    uint8_t* buf = (uint8_t*)malloc(len);
    if (!buf) {
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    int received = 0;
    unsigned long startMs = millis();
    while (received < len && millis() - startMs < 8000) {
        int avail = stream->available();
        if (avail > 0) {
            int toRead = min(avail, len - received);
            received += stream->readBytes(buf + received, toRead);
        } else {
            delay(10);
        }
    }
    http.end();

    if (received != len) {
        free(buf);
        return false;
    }

    // Reused every time art changes; a stale frame may flash briefly mid-decode, which is an
    // acceptable tradeoff for not needing a second full-size buffer on a board with no PSRAM.
    memset(g_artBuffer, 0, (size_t)g_artBufW * g_artBufH * sizeof(uint16_t));
    g_decodeTarget = g_artBuffer;
    g_decodeW = g_artBufW;
    g_decodeH = g_artBufH;

    TJpgDec.setJpgScale(1);
    TJpgDec.setCallback(jpegOutputCallback);
    bool ok = TJpgDec.drawJpg(0, 0, buf, len) == JDR_OK;

    free(buf);
    g_decodeTarget = nullptr;

    return ok;
}

static void publishState(DisplayMode mode, const Session* sessions, int count, bool artValid) {
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    g_state.mode = mode;
    g_state.sessionCount = count;
    for (int i = 0; i < count; i++) g_state.sessions[i] = sessions[i];
    if (artValid) {
        g_state.artBuffer = g_artBuffer;
        g_state.artWidth = g_artBufW;
        g_state.artHeight = g_artBufH;
        g_state.artValid = true;
    } else {
        g_state.artValid = false;
    }
    xSemaphoreGive(g_mutex);
    g_dirty = true;
}

static void networkTaskFn(void* param) {
    (void)param;

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int consecutiveFailures = 0;

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        Session sessions[MAX_SESSIONS];
        int count = 0;
        FetchResult result = fetchSessions(sessions, MAX_SESSIONS, &count);

        if (result == FetchResult::NETWORK_ERROR) {
            consecutiveFailures++;
            if (consecutiveFailures >= 2) {
                publishState(DisplayMode::ERROR_SCREEN, nullptr, 0, false);
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        consecutiveFailures = 0;

        if (count == 0) {
            g_lastThumbPath[0] = '\0';
            publishState(DisplayMode::IDLE, sessions, 0, false);
        } else if (count == 1) {
            bool artValid = false;
            if (strcmp(sessions[0].thumbPath, g_lastThumbPath) != 0) {
                if (fetchAndDecodeArt(sessions[0].thumbPath)) {
                    strlcpy(g_lastThumbPath, sessions[0].thumbPath, sizeof(g_lastThumbPath));
                    artValid = true;
                }
            } else {
                artValid = true; // unchanged art, buffer already holds the right pixels
            }
            publishState(DisplayMode::SINGLE, sessions, 1, artValid);
        } else {
            g_lastThumbPath[0] = '\0';
            publishState(DisplayMode::TABLE, sessions, count, false);
        }

        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

void networkTaskStart() {
    g_mutex = xSemaphoreCreateMutex();

    bool hasPsram = ESP.getPsramSize() > 0;
    if (hasPsram) {
        g_artBufW = 320;
        g_artBufH = 240;
        g_artBuffer = (uint16_t*)heap_caps_malloc(
            (size_t)g_artBufW * g_artBufH * sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    } else {
        g_artBufW = 200;
        g_artBufH = 150;
        g_artBuffer = (uint16_t*)malloc((size_t)g_artBufW * g_artBufH * sizeof(uint16_t));
    }

    xTaskCreatePinnedToCore(networkTaskFn, "networkTask", 8192, nullptr, 1, nullptr, 0);
}

bool networkTaskPollUpdate(SharedState* outState) {
    if (!g_dirty) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    *outState = g_state;
    g_dirty = false;
    xSemaphoreGive(g_mutex);
    return true;
}
