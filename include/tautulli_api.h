#pragma once

#include <Arduino.h>
#include <HTTPClient.h>

#include "plex_api.h" // Session, RecentView, FetchResult, MAX_SESSIONS, MAX_RECENT_VIEWS

// GETs Tautulli's /api/v2 endpoint with `cmdAndParams` appended as-is (e.g.
// "cmd=get_activity"), trying TAUTULLI_LOCAL_IP first (short timeout, plain HTTP) and falling
// back to TAUTULLI_FUNNEL_HOST (HTTPS, longer timeout) if that fails - mirrors plexHttpGet's
// local-LAN-first / Tailscale-Funnel-fallback behavior. Leaves `http` open on success
// (HTTP_CODE_OK) for the caller to read the body; caller must still call http.end().
int tautulliHttpGet(HTTPClient& http, const String& cmdAndParams,
                     unsigned long localTimeoutMs = 1200, unsigned long funnelTimeoutMs = 3000);

// Polls Tautulli's get_activity endpoint (replaces Plex's /status/sessions) and fills `out`
// (capacity `maxSessions`). On success returns FetchResult::OK and sets *count (may be 0 if
// nobody is streaming). On failure (unreachable, timeout, bad response) returns
// FetchResult::NETWORK_ERROR. Session.username is Tautulli's "friendly_name" - a cleaner
// display name than Plex's raw account username.
FetchResult fetchSessions(Session* out, int maxSessions, int* count);

// Fetches recent watch history via Tautulli's get_history endpoint, collapsed to one entry per
// distinct user (most recent first), up to `maxViews`. This is historical/informational, not
// live playback state - there's no progress/duration here, just who watched what most recently.
FetchResult fetchRecentViews(RecentView* out, int maxViews, int* count);
