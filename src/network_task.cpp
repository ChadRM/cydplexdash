#include "network_task.h"

#include <HTTPClient.h>
#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <time.h>

#include "secrets.h"

static const unsigned long IDLE_SCREENSAVER_MS = 5UL * 60UL * 1000UL; // 5 minutes

// Unsynced ESP32 clocks read as far in the past (near epoch 0), so treat anything past this
// (~Sept 2020) as "NTP has synced at least once".
static bool isTimeSynced() {
    return time(nullptr) > 1600000000;
}

static bool isNightHours(int hour) {
    int start = NIGHT_MODE_START_HOUR;
    int end = NIGHT_MODE_END_HOUR;
    if (start == end) return false; // degenerate config - treat as always off
    if (start < end) return hour >= start && hour < end;
    return hour >= start || hour < end; // range wraps past midnight, e.g. 22 -> 8
}

static SemaphoreHandle_t g_mutex;
static SharedState g_state = {};
static volatile bool g_dirty = false;

static uint16_t* g_artBuffer = nullptr;
static int g_artBufW = 0;
static int g_artBufH = 0;
static char g_lastThumbPath[128] = "";

// Scratch buffer for the compressed JPEG bytes, allocated once at startup (not per-fetch)
// so a fragmented heap can't fail the allocation after the device has been running a while.
static uint8_t* g_jpegScratch = nullptr;
static size_t g_jpegScratchCap = 0;

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
        Serial.printf("[art] GET failed, HTTP code %d\n", code);
        http.end();
        return false;
    }

    // Plex's photo transcoder resizes on the fly and often replies without a Content-Length
    // (chunked encoding), so http.getSize() can be -1. Read until the stream stops producing
    // data instead of trusting a declared length.
    WiFiClient* stream = http.getStreamPtr();
    size_t received = 0;
    unsigned long startMs = millis();
    unsigned long lastDataMs = millis();
    while (millis() - startMs < 10000) {
        int avail = stream->available();
        if (avail > 0) {
            size_t toRead = (size_t)avail;
            if (received + toRead > g_jpegScratchCap) toRead = g_jpegScratchCap - received;
            if (toRead == 0) break; // buffer full
            received += stream->readBytes(g_jpegScratch + received, toRead);
            lastDataMs = millis();
        } else if (!http.connected()) {
            break; // server closed the connection, nothing left to read
        } else if (millis() - lastDataMs > 2000) {
            break; // no new data for a while - treat as end of response
        } else {
            delay(5);
        }
    }
    http.end();

    Serial.printf("[art] fetched %u bytes\n", (unsigned)received);

    if (received == 0) {
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
    bool ok = TJpgDec.drawJpg(0, 0, g_jpegScratch, received) == JDR_OK;
    if (!ok) Serial.println("[art] JPEG decode failed");

    g_decodeTarget = nullptr;

    return ok;
}

static RecentView g_cachedRecentViews[MAX_RECENT_VIEWS];
static int g_cachedRecentViewCount = 0;

static void publishState(DisplayMode mode, const Session* sessions, int count, bool artValid,
                          bool screensaverActive) {
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
    if (mode == DisplayMode::IDLE) {
        g_state.recentViewCount = g_cachedRecentViewCount;
        for (int i = 0; i < g_cachedRecentViewCount; i++) g_state.recentViews[i] = g_cachedRecentViews[i];
    } else {
        g_state.recentViewCount = 0;
    }
    g_state.screensaverActive = screensaverActive;
    xSemaphoreGive(g_mutex);
    g_dirty = true;
}

static void networkTaskFn(void* param) {
    (void)param;

    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.printf("[net] connecting to WiFi \"%s\"...\n", WIFI_SSID);

    int consecutiveFailures = 0;
    bool loggedConnected = false;
    unsigned long lastActiveMs = millis(); // "something playing" timer, for the idle screensaver
    // Sentinel "not yet published" value (never actually set by the branches below) so the
    // very first idle observation after boot correctly counts as "just became idle".
    DisplayMode lastMode = DisplayMode::ERROR_SCREEN;
    unsigned long lastRecentViewsFetchMs = 0;
    static const unsigned long RECENT_VIEWS_REFRESH_MS = 60UL * 1000UL; // it's informational, not live

    for (;;) {
        if (WiFi.status() != WL_CONNECTED) {
            loggedConnected = false;
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        if (!loggedConnected) {
            Serial.printf("[net] WiFi connected, IP %s\n", WiFi.localIP().toString().c_str());
            configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
            loggedConnected = true;
        }

        bool nightNow = false;
        if (isTimeSynced()) {
            time_t now = time(nullptr);
            struct tm timeinfo;
            localtime_r(&now, &timeinfo);
            nightNow = isNightHours(timeinfo.tm_hour);
        }
        bool idleTooLong = (millis() - lastActiveMs) >= IDLE_SCREENSAVER_MS;
        bool screensaverActive = nightNow || idleTooLong;

        Session sessions[MAX_SESSIONS];
        int count = 0;
        FetchResult result = fetchSessions(sessions, MAX_SESSIONS, &count);

        if (result == FetchResult::NETWORK_ERROR) {
            consecutiveFailures++;
            Serial.printf("[net] Plex poll failed (%d consecutive)\n", consecutiveFailures);
            if (consecutiveFailures >= 2) {
                publishState(DisplayMode::ERROR_SCREEN, nullptr, 0, false, screensaverActive);
            }
            vTaskDelay(pdMS_TO_TICKS(3000));
            continue;
        }

        consecutiveFailures = 0;
        Serial.printf("[net] poll ok, %d session(s)\n", count);

        if (count == 0) {
            g_lastThumbPath[0] = '\0';

            // Recent-views is informational, not live - only (re)fetch it when we just became
            // idle, or every minute or so while remaining idle, not on every 3s poll.
            bool justBecameIdle = (lastMode != DisplayMode::IDLE);
            if (justBecameIdle || (millis() - lastRecentViewsFetchMs > RECENT_VIEWS_REFRESH_MS)) {
                RecentView views[MAX_RECENT_VIEWS];
                int rvCount = 0;
                if (fetchRecentViews(views, MAX_RECENT_VIEWS, &rvCount) == FetchResult::OK) {
                    g_cachedRecentViewCount = rvCount;
                    for (int i = 0; i < rvCount; i++) g_cachedRecentViews[i] = views[i];
                    Serial.printf("[net] recent views refreshed, %d found\n", rvCount);
                }
                lastRecentViewsFetchMs = millis();
            }

            publishState(DisplayMode::IDLE, sessions, 0, false, screensaverActive);
            lastMode = DisplayMode::IDLE;
        } else if (count == 1) {
            lastActiveMs = millis();
            bool artValid = false;
            if (strcmp(sessions[0].thumbPath, g_lastThumbPath) != 0) {
                Serial.printf("[art] thumb changed, fetching: %s\n", sessions[0].thumbPath);
                if (fetchAndDecodeArt(sessions[0].thumbPath)) {
                    strlcpy(g_lastThumbPath, sessions[0].thumbPath, sizeof(g_lastThumbPath));
                    artValid = true;
                }
            } else {
                artValid = true; // unchanged art, buffer already holds the right pixels
            }
            publishState(DisplayMode::SINGLE, sessions, 1, artValid, screensaverActive);
            lastMode = DisplayMode::SINGLE;
        } else {
            lastActiveMs = millis();
            g_lastThumbPath[0] = '\0';
            publishState(DisplayMode::TABLE, sessions, count, false, screensaverActive);
            lastMode = DisplayMode::TABLE;
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
        g_jpegScratchCap = 100000; // PSRAM is plentiful, generous headroom for the compressed JPEG
        g_jpegScratch = (uint8_t*)heap_caps_malloc(g_jpegScratchCap, MALLOC_CAP_SPIRAM);
    } else {
        g_artBufW = 200;
        g_artBufH = 150;
        g_artBuffer = (uint16_t*)malloc((size_t)g_artBufW * g_artBufH * sizeof(uint16_t));
        // Internal RAM is scarce here; a 200x150 JPEG thumbnail rarely exceeds ~20KB, so 40KB
        // is generous without competing much with WiFi/JSON parsing for the same heap.
        g_jpegScratchCap = 40000;
        g_jpegScratch = (uint8_t*)malloc(g_jpegScratchCap);
    }

    Serial.printf("[net] PSRAM: %s, art buffer %dx%d, JPEG scratch %u bytes, free heap %u\n",
                  hasPsram ? "yes" : "no", g_artBufW, g_artBufH, (unsigned)g_jpegScratchCap,
                  (unsigned)ESP.getFreeHeap());

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
