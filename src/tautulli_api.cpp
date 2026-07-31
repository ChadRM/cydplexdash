#include "tautulli_api.h"

#include <ArduinoJson.h>
#include <WiFiClientSecure.h>

#include "secrets.h"

static PlayState parseState(const char* s) {
    if (!s) return PlayState::UNKNOWN;
    if (strcmp(s, "playing") == 0) return PlayState::PLAYING;
    if (strcmp(s, "paused") == 0) return PlayState::PAUSED;
    if (strcmp(s, "buffering") == 0) return PlayState::BUFFERING;
    return PlayState::UNKNOWN;
}

// Tautulli's API returns numeric fields (duration, view_offset) as JSON strings, not numbers -
// parse explicitly rather than relying on ArduinoJson's numeric-string coercion.
static uint32_t parseMs(const char* s) {
    return s ? (uint32_t)atol(s) : 0;
}

int tautulliHttpGet(HTTPClient& http, const String& cmdAndParams, unsigned long localTimeoutMs,
                     unsigned long funnelTimeoutMs) {
    String path = "/api/v2?apikey=" + String(TAUTULLI_API_KEY) + "&" + cmdAndParams;

    String localUrl = String("http://") + TAUTULLI_LOCAL_IP + ":" + String(TAUTULLI_PORT) + path;
    http.begin(localUrl);
    http.setConnectTimeout(localTimeoutMs);
    http.setTimeout(localTimeoutMs);
    int code = http.GET();
    if (code == HTTP_CODE_OK) return code;
    http.end();

    String funnelUrl = String("https://") + TAUTULLI_FUNNEL_HOST + path;
    // Same rationale as plexHttpGet: skip cert validation, since NTP may not have synced yet
    // this early in boot, and TAUTULLI_API_KEY is already the real auth boundary here.
    static WiFiClientSecure secureClient;
    secureClient.setInsecure();
    http.begin(secureClient, funnelUrl);
    http.setConnectTimeout(funnelTimeoutMs);
    http.setTimeout(funnelTimeoutMs);
    return http.GET();
}

FetchResult fetchSessions(Session* out, int maxSessions, int* count) {
    *count = 0;

    HTTPClient http;
    int httpCode = tautulliHttpGet(http, "cmd=get_activity");
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return FetchResult::NETWORK_ERROR;
    }

    // Filter keeps only the fields we use, so the parsed doc stays small even with several
    // concurrent sessions and no PSRAM to fall back on.
    JsonDocument filter;
    JsonObject item = filter["response"]["data"]["sessions"].add<JsonObject>();
    item["friendly_name"] = true;
    item["title"] = true;
    item["grandparent_title"] = true;
    item["parent_title"] = true;
    item["duration"] = true;
    item["view_offset"] = true;
    item["thumb"] = true;
    item["grandparent_thumb"] = true;
    item["state"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        return FetchResult::NETWORK_ERROR;
    }

    JsonArray sessions = doc["response"]["data"]["sessions"].as<JsonArray>();
    int n = 0;
    for (JsonObject entry : sessions) {
        if (n >= maxSessions) break;
        Session& s = out[n];
        memset(&s, 0, sizeof(Session));

        const char* user = entry["friendly_name"] | "Unknown";
        strlcpy(s.username, user, sizeof(s.username));

        const char* title = entry["title"] | "";
        strlcpy(s.title, title, sizeof(s.title));

        const char* grandparent = entry["grandparent_title"] | "";
        const char* parent = entry["parent_title"] | "";
        if (grandparent[0] && parent[0]) {
            snprintf(s.subtitle, sizeof(s.subtitle), "%s - %s", grandparent, parent);
        } else if (grandparent[0]) {
            strlcpy(s.subtitle, grandparent, sizeof(s.subtitle));
        } else if (parent[0]) {
            strlcpy(s.subtitle, parent, sizeof(s.subtitle));
        }

        const char* thumb = entry["grandparent_thumb"] | (const char*)nullptr;
        if (!thumb) thumb = entry["thumb"] | "";
        strlcpy(s.thumbPath, thumb, sizeof(s.thumbPath));

        s.durationMs = parseMs(entry["duration"] | (const char*)nullptr);
        s.progressMs = parseMs(entry["view_offset"] | (const char*)nullptr);
        s.state = parseState(entry["state"] | "");

        n++;
    }

    *count = n;
    return FetchResult::OK;
}

static const int HISTORY_FETCH_SIZE = 20; // enough to usually find 2 distinct recent users

FetchResult fetchRecentViews(RecentView* out, int maxViews, int* count) {
    *count = 0;

    // Most-recent-first watch history. Unlike Plex's /status/sessions/history/all, Tautulli's
    // get_history already includes each row's friendly_name directly - no separate
    // account-ID-to-username lookup pass needed.
    String historyPath = "cmd=get_history&order_column=date&order_dir=desc&length=" +
                          String(HISTORY_FETCH_SIZE);

    HTTPClient http;
    int httpCode = tautulliHttpGet(http, historyPath);
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return FetchResult::NETWORK_ERROR;
    }

    JsonDocument filter;
    JsonObject item = filter["response"]["data"]["data"].add<JsonObject>();
    item["friendly_name"] = true;
    item["title"] = true;
    item["grandparent_title"] = true;
    item["parent_title"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        return FetchResult::NETWORK_ERROR;
    }

    // Collapse to one entry per distinct user, in most-recent-first order.
    int n = 0;
    JsonArray rows = doc["response"]["data"]["data"].as<JsonArray>();
    for (JsonObject entry : rows) {
        if (n >= maxViews) break;

        const char* user = entry["friendly_name"] | (const char*)nullptr;
        if (!user || !user[0]) continue;

        bool alreadySeen = false;
        for (int i = 0; i < n; i++) {
            if (strcmp(out[i].username, user) == 0) {
                alreadySeen = true;
                break;
            }
        }
        if (alreadySeen) continue;

        RecentView& v = out[n];
        memset(&v, 0, sizeof(RecentView));
        strlcpy(v.username, user, sizeof(v.username));

        const char* title = entry["title"] | "";
        strlcpy(v.title, title, sizeof(v.title));

        const char* grandparent = entry["grandparent_title"] | "";
        const char* parent = entry["parent_title"] | "";
        if (grandparent[0] && parent[0]) {
            snprintf(v.subtitle, sizeof(v.subtitle), "%s - %s", grandparent, parent);
        } else if (grandparent[0]) {
            strlcpy(v.subtitle, grandparent, sizeof(v.subtitle));
        } else if (parent[0]) {
            strlcpy(v.subtitle, parent, sizeof(v.subtitle));
        }

        n++;
    }

    *count = n;
    return FetchResult::OK;
}
