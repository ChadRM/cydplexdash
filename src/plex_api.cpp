#include "plex_api.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

#include "secrets.h"

static PlayState parseState(const char* s) {
    if (!s) return PlayState::UNKNOWN;
    if (strcmp(s, "playing") == 0) return PlayState::PLAYING;
    if (strcmp(s, "paused") == 0) return PlayState::PAUSED;
    if (strcmp(s, "buffering") == 0) return PlayState::BUFFERING;
    return PlayState::UNKNOWN;
}

static String urlEncode(const char* s) {
    String encoded;
    char buf[4];
    while (*s) {
        unsigned char c = (unsigned char)*s++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            encoded += (char)c;
        } else {
            snprintf(buf, sizeof(buf), "%%%02X", c);
            encoded += buf;
        }
    }
    return encoded;
}

FetchResult fetchSessions(Session* out, int maxSessions, int* count) {
    *count = 0;

    String url = String("http://") + PLEX_SERVER_IP + ":" + String(PLEX_SERVER_PORT) +
                  "/status/sessions?X-Plex-Token=" + PLEX_TOKEN;

    HTTPClient http;
    http.begin(url);
    http.addHeader("Accept", "application/json");
    http.setConnectTimeout(3000);
    http.setTimeout(3000);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return FetchResult::NETWORK_ERROR;
    }

    // Filter keeps only the fields we use, so the parsed doc stays small even with
    // several concurrent sessions and no PSRAM to fall back on.
    JsonDocument filter;
    JsonObject item = filter["MediaContainer"]["Metadata"].add<JsonObject>();
    item["title"] = true;
    item["grandparentTitle"] = true;
    item["parentTitle"] = true;
    item["duration"] = true;
    item["viewOffset"] = true;
    item["thumb"] = true;
    item["grandparentThumb"] = true;
    item["User"]["title"] = true;
    item["Player"]["state"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        return FetchResult::NETWORK_ERROR;
    }

    JsonArray metadata = doc["MediaContainer"]["Metadata"].as<JsonArray>();
    int n = 0;
    for (JsonObject entry : metadata) {
        if (n >= maxSessions) break;
        Session& s = out[n];
        memset(&s, 0, sizeof(Session));

        const char* user = entry["User"]["title"] | "Unknown";
        strlcpy(s.username, user, sizeof(s.username));

        const char* title = entry["title"] | "";
        strlcpy(s.title, title, sizeof(s.title));

        const char* grandparent = entry["grandparentTitle"] | "";
        const char* parent = entry["parentTitle"] | "";
        if (grandparent[0] && parent[0]) {
            snprintf(s.subtitle, sizeof(s.subtitle), "%s - %s", grandparent, parent);
        } else if (grandparent[0]) {
            strlcpy(s.subtitle, grandparent, sizeof(s.subtitle));
        } else if (parent[0]) {
            strlcpy(s.subtitle, parent, sizeof(s.subtitle));
        }

        const char* thumb = entry["grandparentThumb"] | (const char*)nullptr;
        if (!thumb) thumb = entry["thumb"] | "";
        strlcpy(s.thumbPath, thumb, sizeof(s.thumbPath));

        s.durationMs = entry["duration"] | 0;
        s.progressMs = entry["viewOffset"] | 0;
        s.state = parseState(entry["Player"]["state"] | "");

        n++;
    }

    *count = n;
    return FetchResult::OK;
}

String buildArtUrl(const char* thumbPath, int width, int height) {
    String fullPath = String("http://") + PLEX_SERVER_IP + ":" + String(PLEX_SERVER_PORT) + thumbPath;
    String url = String("http://") + PLEX_SERVER_IP + ":" + String(PLEX_SERVER_PORT) +
                 "/photo/:/transcode?width=" + String(width) + "&height=" + String(height) +
                 "&url=" + urlEncode(fullPath.c_str()) + "&X-Plex-Token=" + PLEX_TOKEN;
    return url;
}
