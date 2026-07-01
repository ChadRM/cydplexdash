# cydplexdash

A Plex now-playing dashboard for the ESP32-2432S028R ("Cheap Yellow Display" / CYD) - a
$10 board with a 320x240 ILI9341 touch LCD, WiFi/BT, and a dual-core ESP32.

It polls your local Plex Media Server and shows:
- **Nobody streaming** - an idle screen.
- **One active session** - full-screen cover art with a title/artist/progress overlay.
- **Multiple active sessions** - a dark-themed table, one row per user (who's watching what).
- **Plex server unreachable** - a red warning screen, until it recovers.

The server's name is always shown in a thin bar across the top of every screen.

## Hardware

- Board: ESP32-2432S028R (ILI9341 SPI display, resistive touch not used by this project).
- Display pins (fixed via `platformio.ini` build flags, no wiring needed): standard for
  this exact board - MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, BL 21.
- If colors look inverted on your board batch, see the comment in `platformio.ini` about
  switching `ILI9341_DRIVER` to `ILI9341_2_DRIVER`.

## Prerequisites

- [PlatformIO](https://platformio.org/) - either the CLI (`pip install platformio`) or the
  VS Code extension.
- A USB cable to the board's USB-C port (also used for flashing/serial).
- Your Plex Media Server's local IP/hostname and an
  [X-Plex-Token](https://support.plex.tv/articles/204059436-finding-an-authentication-token-x-plex-token/).

## Setup

1. Copy the secrets template and fill in your real values:

   ```
   cp include/secrets.h.example include/secrets.h
   ```

   Edit `include/secrets.h`:

   ```c
   #define WIFI_SSID "YourWiFiName"
   #define WIFI_PASSWORD "YourWiFiPassword"

   #define PLEX_SERVER_IP "192.168.1.50"   // local IP or hostname of your Plex server
   #define PLEX_SERVER_PORT 32400
   #define PLEX_SERVER_NAME "MyPlexServer" // shown in the top bar on every screen

   #define PLEX_TOKEN "your-plex-token-here"
   ```

   `secrets.h` is gitignored - it never gets committed.

2. Plug in the board via USB and find its serial port:

   ```
   pio device list
   ```

   Look for a CH340/CP210x USB-serial entry (e.g. `COM8` on Windows, `/dev/ttyUSB0` on Linux).

3. Build and flash:

   ```
   pio run -t upload --upload-port <PORT>
   ```

4. Watch it boot (optional, useful for troubleshooting):

   ```
   pio device monitor --port <PORT> --baud 115200
   ```

   You should see WiFi connect, then periodic Plex poll results and (when something's
   playing) art fetch logs.

## Notes

- No PSRAM is assumed by default; the code detects PSRAM at boot and uses a larger cover-art
  buffer automatically if present.
- Polling interval is 3 seconds; the Plex server error screen appears after 2 consecutive
  failed polls (~6s) to avoid flickering on a single transient blip.
- Uses both ESP32 cores: core 0 handles WiFi/Plex polling and JPEG decode, core 1 runs the
  LVGL UI loop.
