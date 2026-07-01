#pragma once

#include "network_task.h"
#include "plex_api.h"

void ui_init();

void ui_update(DisplayMode mode, const Session* sessions, int count, const uint16_t* artBuffer,
               int artWidth, int artHeight);

// Updates the clock shown in the top bar, opposite the server name. Called independently of
// ui_update() since the clock ticks every second regardless of Plex poll state.
void ui_set_clock(const char* text);
