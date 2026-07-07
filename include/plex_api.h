#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

#define MAX_SESSIONS 8
#define MAX_RECENT_VIEWS 2

enum class PlayState : uint8_t {
    PLAYING,
    PAUSED,
    BUFFERING,
    UNKNOWN
};

struct Session {
    char username[32];
    char title[64];      // episode/track/movie title
    char subtitle[80];   // "Show - Season" / "Artist - Album", pre-combined
    char thumbPath[128]; // relative Plex path used to build the art URL, empty if none
    uint32_t progressMs;
    uint32_t durationMs;
    PlayState state;
};

enum class FetchResult : uint8_t {
    OK,
    NETWORK_ERROR
};

// Polls the Plex server's /status/sessions endpoint and fills `out` (capacity `maxSessions`).
// On success returns FetchResult::OK and sets *count (may be 0 if nobody is streaming).
// On failure (unreachable, timeout, bad response) returns FetchResult::NETWORK_ERROR.
FetchResult fetchSessions(Session* out, int maxSessions, int* count);

// Builds a Plex photo-transcode path+query (no host/port - see plexHttpGet) that resizes the
// given session's art to width x height.
String buildArtPath(const char* thumbPath, int width, int height);

// GETs `path` from the Plex server, trying PLEX_LOCAL_IP first (short timeout, plain HTTP) and
// falling back to PLEX_FUNNEL_HOST (HTTPS, longer timeout) if that fails - so the device is fast
// on the home LAN and still works away from home via Tailscale Funnel. `jsonAccept` adds an
// `Accept: application/json` header (skip for binary responses like the art JPEG). Leaves `http`
// open on success (HTTP_CODE_OK) for the caller to read the body/stream; caller must still call
// http.end().
int plexHttpGet(HTTPClient& http, const String& path, bool jsonAccept = true,
                 unsigned long localTimeoutMs = 1200, unsigned long funnelTimeoutMs = 3000);

struct RecentView {
    char username[32];
    char title[64];
    char subtitle[80];
};

// Fetches the most recent watch history, collapsed to one entry per distinct user (most
// recent view first), up to `maxViews`. This is historical/informational, not live playback
// state - there's no progress/duration here, just who watched what most recently.
FetchResult fetchRecentViews(RecentView* out, int maxViews, int* count);
