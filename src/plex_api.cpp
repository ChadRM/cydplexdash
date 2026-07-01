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

struct HistoryEntry {
    int accountID;
    char title[64];
    char subtitle[80];
};

static const int HISTORY_FETCH_SIZE = 20; // enough to usually find 2 distinct recent users

FetchResult fetchRecentViews(RecentView* out, int maxViews, int* count) {
    *count = 0;

    // 1. Fetch recent watch history, most recent first.
    String historyUrl = String("http://") + PLEX_SERVER_IP + ":" + String(PLEX_SERVER_PORT) +
                         "/status/sessions/history/all?sort=viewedAt:desc&X-Plex-Container-Start=0"
                         "&X-Plex-Container-Size=" +
                         String(HISTORY_FETCH_SIZE) + "&X-Plex-Token=" + PLEX_TOKEN;

    HTTPClient http;
    http.begin(historyUrl);
    http.addHeader("Accept", "application/json");
    http.setConnectTimeout(3000);
    http.setTimeout(3000);

    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        http.end();
        return FetchResult::NETWORK_ERROR;
    }

    JsonDocument filter;
    JsonObject item = filter["MediaContainer"]["Metadata"].add<JsonObject>();
    item["accountID"] = true;
    item["title"] = true;
    item["grandparentTitle"] = true;
    item["parentTitle"] = true;

    JsonDocument doc;
    DeserializationError err =
        deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
    http.end();

    if (err) {
        return FetchResult::NETWORK_ERROR;
    }

    // Collapse to one entry per distinct account, in most-recent-first order.
    HistoryEntry entries[MAX_RECENT_VIEWS];
    int n = 0;
    JsonArray metadata = doc["MediaContainer"]["Metadata"].as<JsonArray>();
    for (JsonObject entry : metadata) {
        if (n >= maxViews) break;

        int accountID = entry["accountID"] | -1;
        if (accountID < 0) continue;

        bool alreadySeen = false;
        for (int i = 0; i < n; i++) {
            if (entries[i].accountID == accountID) {
                alreadySeen = true;
                break;
            }
        }
        if (alreadySeen) continue;

        HistoryEntry& h = entries[n];
        h.accountID = accountID;

        const char* title = entry["title"] | "";
        strlcpy(h.title, title, sizeof(h.title));

        h.subtitle[0] = '\0';
        const char* grandparent = entry["grandparentTitle"] | "";
        const char* parent = entry["parentTitle"] | "";
        if (grandparent[0] && parent[0]) {
            snprintf(h.subtitle, sizeof(h.subtitle), "%s - %s", grandparent, parent);
        } else if (grandparent[0]) {
            strlcpy(h.subtitle, grandparent, sizeof(h.subtitle));
        } else if (parent[0]) {
            strlcpy(h.subtitle, parent, sizeof(h.subtitle));
        }

        n++;
    }

    if (n == 0) {
        *count = 0;
        return FetchResult::OK;
    }

    // 2. Resolve accountID -> username. Best-effort: if this call fails, still return the
    // titles/subtitles we already have, just with "Unknown" in place of a name.
    String acctUrl = String("http://") + PLEX_SERVER_IP + ":" + String(PLEX_SERVER_PORT) +
                      "/accounts?X-Plex-Token=" + PLEX_TOKEN;

    HTTPClient acctHttp;
    acctHttp.begin(acctUrl);
    acctHttp.addHeader("Accept", "application/json");
    acctHttp.setConnectTimeout(3000);
    acctHttp.setTimeout(3000);

    int acctCode = acctHttp.GET();
    JsonDocument acctDoc;
    bool haveAccounts = false;
    if (acctCode == HTTP_CODE_OK) {
        JsonDocument acctFilter;
        JsonObject acctItem = acctFilter["MediaContainer"]["Account"].add<JsonObject>();
        acctItem["id"] = true;
        acctItem["name"] = true;

        DeserializationError acctErr = deserializeJson(acctDoc, acctHttp.getStream(),
                                                        DeserializationOption::Filter(acctFilter));
        haveAccounts = !acctErr;
    }
    acctHttp.end();

    for (int i = 0; i < n; i++) {
        RecentView& v = out[i];
        memset(&v, 0, sizeof(RecentView));
        strlcpy(v.title, entries[i].title, sizeof(v.title));
        strlcpy(v.subtitle, entries[i].subtitle, sizeof(v.subtitle));
        strlcpy(v.username, "Unknown", sizeof(v.username));

        if (haveAccounts) {
            JsonArray accounts = acctDoc["MediaContainer"]["Account"].as<JsonArray>();
            for (JsonObject acct : accounts) {
                int id = acct["id"] | -1;
                if (id == entries[i].accountID) {
                    const char* name = acct["name"] | "Unknown";
                    strlcpy(v.username, name, sizeof(v.username));
                    break;
                }
            }
        }
    }

    *count = n;
    return FetchResult::OK;
}

String buildArtUrl(const char* thumbPath, int width, int height) {
    // The transcoder's "url" param wants the plain library-relative path (e.g.
    // "/library/metadata/123/thumb/456"), resolved internally - passing a full http://host:port
    // URL here 404s, since Plex treats that as an external image reference instead.
    String url = String("http://") + PLEX_SERVER_IP + ":" + String(PLEX_SERVER_PORT) +
                 "/photo/:/transcode?width=" + String(width) + "&height=" + String(height) +
                 "&url=" + urlEncode(thumbPath) + "&X-Plex-Token=" + PLEX_TOKEN;
    return url;
}
