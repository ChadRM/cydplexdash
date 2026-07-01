# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A PlatformIO/Arduino firmware project for the ESP32-2432S028R ("Cheap Yellow Display" / CYD) -
a $10 board with a 320x240 ILI9341 touch LCD. It polls a local Plex Media Server and shows a
now-playing dashboard: idle screen, full-screen cover art for one session, a scrollable table
for multiple concurrent sessions, or a red error screen when Plex is unreachable. See
README.md for the full feature/setup description (secrets, touch calibration, hardware pins).

## Commands

```
pio run                                    # build
pio run -t upload --upload-port <PORT>     # flash (find PORT via `pio device list`)
pio device monitor --port <PORT> --baud 115200   # serial log (WiFi connect, poll results, art fetch, touch coords)
```

There is no test suite - this is device firmware; validate changes by flashing and watching
the serial monitor / physical screen.

`include/secrets.h` (gitignored, copy from `secrets.h.example`) holds WiFi credentials, the
Plex server IP/port/token, and NTP/night-mode config - required for the project to build.

### Regenerating bitmap fonts

Fonts in `src/fonts/*.c` are pre-generated LVGL bitmap fonts, not built from source at compile
time. Each file's header comment records the exact `lv_font_conv` invocation used to produce
it (source font, codepoint range, size, bpp). To add/change one, re-run `npx lv_font_conv` with
those options and drop the output back into `src/fonts/`, then add an `extern const lv_font_t`
declaration in `include/jetbrains_mono_fonts.h`. `lv_font_conv` only accepts ttf/woff (not
woff2); npm font packages often ship only woff2, so decompress first (e.g. the `wawoff2` npm
package) if needed. Icon glyphs (e.g. `src/fonts/status_icons_14.c`) are pulled from Font
Awesome Free Solid at the same codepoints as LVGL's `LV_SYMBOL_*` macros, so those macros can
be used directly as the string value once the icon font is applied to a label/cell.

## Architecture

**Two FreeRTOS tasks split by core**, communicating through one mutex-guarded struct:

- **Core 0** (`network_task.cpp`, `networkTaskFn`): owns WiFi, polls Plex's
  `/status/sessions` every 3s (`plex_api.cpp`), fetches/decodes cover art JPEGs
  (`TJpg_Decoder`), and computes screensaver/night-mode state. On every poll it builds a fresh
  `SharedState` and publishes it via `publishState()` (takes `g_mutex`, copies in, sets
  `g_dirty`).
- **Core 1** (`main.cpp` `loop()` + `ui.cpp`): runs `lv_timer_handler()` every iteration, calls
  `networkTaskPollUpdate()` to non-blockingly check `g_dirty` and copy out a `SharedState` when
  new data exists, then hands it to `ui_update()`. Also drives touch input polling and the
  physical backlight pin.

`SharedState` (`network_task.h`) is the entire contract between the two cores: a
`DisplayMode` enum (ERROR_SCREEN/IDLE/SINGLE/TABLE), up to `MAX_SESSIONS` `Session` structs,
the decoded art buffer (owned by the network task, only valid when `artValid`), cached
`RecentView` entries for the idle screen, and `screensaverActive`. Never hold a pointer into it
across a poll boundary - `networkTaskPollUpdate()` copies by value into caller-owned memory.

**`ui.cpp`** pre-creates all four view containers once in `ui_init()` (never destroyed/rebuilt)
and `ui_update()` just hides all but the active one and repopulates its widgets/labels/table
cells. Per-cell table styling (row/column-specific colors, fonts, alignment) is done in the
`LV_EVENT_DRAW_PART_BEGIN` callback (`tableDrawPartEventCb`), not via per-cell LVGL styles,
since `lv_table` cells don't support that directly - mirror that pattern for any new
conditional cell formatting. Colors are a fixed Tokyo Night palette defined as `COLOR_*`
constants at the top of the file.

**Art buffer sizing** adapts to hardware at startup (`networkTaskStart()`): 320x240 in PSRAM if
present, otherwise a smaller 200x150 buffer in internal RAM, plus a correspondingly-sized JPEG
scratch buffer allocated once (not per-fetch, to avoid heap fragmentation failures over a long
uptime).

**Display/touch pins** are fixed for this exact board via `build_flags` in `platformio.ini`
(TFT) and constants in `main.cpp` (touch) - see the comments there before assuming a generic
CYD pinout applies. Touch runs on a second, independent SPI bus (HSPI) from the display's
(VSPI via `TFT_eSPI`), which is non-standard for most CYD guides but confirmed correct for
this board batch.
