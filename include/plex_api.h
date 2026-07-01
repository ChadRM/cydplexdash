#pragma once

#include <Arduino.h>

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

// Builds a Plex photo-transcode URL that resizes the given session's art to width x height.
String buildArtUrl(const char* thumbPath, int width, int height);

struct RecentView {
    char username[32];
    char title[64];
    char subtitle[80];
};

// Fetches the most recent watch history, collapsed to one entry per distinct user (most
// recent view first), up to `maxViews`. This is historical/informational, not live playback
// state - there's no progress/duration here, just who watched what most recently.
FetchResult fetchRecentViews(RecentView* out, int maxViews, int* count);
