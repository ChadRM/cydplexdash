#pragma once

#include "network_task.h"
#include "plex_api.h"

void ui_init();

void ui_update(DisplayMode mode, const Session* sessions, int count, const uint16_t* artBuffer,
               int artWidth, int artHeight);
