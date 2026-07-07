#pragma once

#include <functional>

// Tries each saved WiFi network (most-recently-successful first) for a few seconds each. If
// none connect, calls `onEnterPortalMode` once (so the caller can surface UI feedback without
// this module needing to know about SharedState/LVGL) and then starts a "CYD-Setup" AP with a
// captive-portal web page for entering new credentials. On success from either path, the
// network is persisted (moved to front) and this function returns with the device connected in
// station mode. A credential save from the portal restarts the device rather than returning.
void wifiManagerConnect(std::function<void()> onEnterPortalMode);
