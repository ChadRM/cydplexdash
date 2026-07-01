#pragma once

#include "plex_api.h"

enum class DisplayMode : uint8_t {
    ERROR_SCREEN,
    IDLE,
    SINGLE,
    TABLE
};

// Snapshot of state safe to read from the UI/core-1 loop.
struct SharedState {
    DisplayMode mode;
    Session sessions[MAX_SESSIONS];
    int sessionCount;

    // Only valid when mode == SINGLE and artValid is true.
    const uint16_t* artBuffer; // RGB565 pixels, artWidth * artHeight, owned by the network task
    int artWidth;
    int artHeight;
    bool artValid;

    // Only meaningful when mode == IDLE. Historical/informational, refreshed occasionally
    // rather than every poll - see fetchRecentViews().
    RecentView recentViews[MAX_RECENT_VIEWS];
    int recentViewCount;

    // True when the display should be dark: nothing has played for 5+ minutes, or it's
    // currently within the configured night-mode hours.
    bool screensaverActive;
};

// Starts the background FreeRTOS task pinned to core 0 (WiFi, Plex polling, JPEG decode).
void networkTaskStart();

// Call from core 1 (Arduino loop). If new data is available since the last call, copies it
// into `outState` and returns true; otherwise returns false and outState is left untouched.
bool networkTaskPollUpdate(SharedState* outState);
