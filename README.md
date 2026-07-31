# cydplexdash

A Plex now-playing dashboard for the ESP32-2432S028R ("Cheap Yellow Display" / CYD) - a
$10 board with a 320x240 ILI9341 touch LCD, WiFi/BT, and a dual-core ESP32.

It polls [Tautulli](https://tautulli.com/) (which must be set up against your Plex server) for
sessions and watch history - Tautulli's per-user "friendly name" is a cleaner display name than
Plex's raw account username - and fetches cover art directly from Plex. It shows:
- **Nobody streaming** - an idle screen.
- **One active session** - full-screen cover art with a title/artist/progress overlay.
- **Multiple active sessions** - a dark-themed table, one row per user (who's watching what).
  Drag on the table to scroll if there are more sessions than fit on screen.
- **Tautulli unreachable** - a red warning screen, until it recovers.
- **WiFi setup** - a blue screen with instructions, shown when the device doesn't recognize
  any nearby network (see [Portable use / WiFi setup](#portable-use--wifi-setup) below).

The device is portable: it remembers up to 5 WiFi networks it has joined before, and reaches
your Tautulli and Plex servers over your home LAN when possible, falling back to their
respective Tailscale Funnel URLs when it isn't.

Every screen has the server's name in a bar across the top-left, and a live clock
(12-hour, e.g. `9:05pm`) in the top-right, synced over NTP.

The screen goes dark (backlight off) after 5 minutes of nothing playing, or during
configurable night hours (10pm-8am by default) - whichever applies first.

## Hardware

- Board: ESP32-2432S028R (ILI9341 SPI display + XPT2046 resistive touch).
- Display pins (fixed via `platformio.ini` build flags, no wiring needed): standard for
  this exact board - MOSI 13, MISO 12, SCLK 14, CS 15, DC 2, BL 21.
- Touch pins: this board wires the XPT2046 touch controller to its **own dedicated SPI
  bus**, separate from the display - MOSI 32, MISO 39, SCK 25, CS 33, IRQ 36 (confirmed
  from the seller's reference firmware; this differs from most CYD guides, which assume a
  shared bus). The firmware runs touch on the ESP32's second hardware SPI peripheral
  (HSPI) so it doesn't interfere with the display's bus.
- If colors look inverted on your board batch, see the comment in `platformio.ini` about
  switching `ILI9341_DRIVER` to `ILI9341_2_DRIVER`.

## Prerequisites

- [PlatformIO](https://platformio.org/) - either the CLI (`pip install platformio`) or the
  VS Code extension.
- A USB cable to the board's USB-C port (also used for flashing/serial).
- Your Plex Media Server's local IP/hostname and an
  [X-Plex-Token](https://support.plex.tv/articles/204059436-finding-an-authentication-token-x-plex-token/)
  (used for cover art).
- A [Tautulli](https://tautulli.com/) instance set up against that Plex server, and its API key
  (Settings > Web Interface > API in Tautulli's UI).

## Setup

1. Copy the secrets template and fill in your real values:

   ```
   cp include/secrets.h.example include/secrets.h
   ```

   Edit `include/secrets.h`:

   ```c
   #define PLEX_LOCAL_IP "192.168.1.50"    // local IP of your Plex server (tried first)
   #define PLEX_SERVER_PORT 32400
   #define PLEX_SERVER_NAME "MyPlexServer" // shown in the top bar on every screen

   // Tailscale Funnel hostname for your Plex server, e.g. "myplexhost.tailxxxx.ts.net" -
   // used as a fallback when PLEX_LOCAL_IP isn't reachable. See "Portable use" below.
   #define PLEX_FUNNEL_HOST "myplexhost.example.ts.net"

   #define PLEX_TOKEN "your-plex-token-here"

   // Tautulli - polled for sessions/watch history instead of Plex directly (cleaner usernames).
   // Local IP of the Tautulli instance (often the same host as Plex).
   #define TAUTULLI_LOCAL_IP "192.168.1.50"
   #define TAUTULLI_PORT 8181
   #define TAUTULLI_API_KEY "your-tautulli-api-key-here"

   // Tailscale Funnel hostname for Tautulli - same idea as PLEX_FUNNEL_HOST above, only needed
   // for portable use. See "Portable use / WiFi setup" below.
   #define TAUTULLI_FUNNEL_HOST "myserver.example.ts.net"

   // NTP time sync (powers the on-screen clock and the night-mode schedule below)
   #define NTP_SERVER "pool.ntp.org"
   #define GMT_OFFSET_SEC (-5 * 3600) // your UTC offset in seconds, standard time
   #define DAYLIGHT_OFFSET_SEC 3600   // 1 hour DST offset, or 0 if your area doesn't observe DST

   // Screen goes dark during these local hours (0-23, wraps past midnight fine)
   #define NIGHT_MODE_START_HOUR 22 // 10:00 PM
   #define NIGHT_MODE_END_HOUR 8    // 8:00 AM
   ```

   `secrets.h` is gitignored - it never gets committed. `PLEX_FUNNEL_HOST` is only needed for
   the portable/away-from-home use case (see below); if you don't use Tailscale, any
   placeholder value is fine - the fallback attempt will just fail harmlessly if it's ever
   used.

   Unlike the other settings, WiFi credentials aren't set in `secrets.h` - see
   [Portable use / WiFi setup](#portable-use--wifi-setup) below.

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

   On first boot (no saved WiFi network yet) the screen shows WiFi setup instructions instead
   of connecting - see below. Once connected, you should see periodic Plex poll results and
   (when something's playing) art fetch logs. Touch presses log their raw coordinates too
   (`[touch] raw x=... y=...`), useful if you ever need to re-tune the calibration bounds.

## Portable use / WiFi setup

The device remembers up to 5 WiFi networks it has joined successfully, most-recent first, and
tries each in turn at boot. If none connect (e.g. it's been moved to a new location), it opens
its own WiFi network, `CYD-Setup`, with a captive-portal page for entering new credentials:

1. Connect a phone or laptop to the `CYD-Setup` network. Most devices auto-open the sign-in
   page; otherwise browse to `http://192.168.4.1`.
2. Pick your WiFi network from the list and enter its password, then submit.
3. The device restarts and joins the new network, remembering it for next time.

There's no way to re-open this portal on demand while a known network is still in range - it's
only triggered automatically when nothing else connects, which covers the normal "moved
somewhere new" case.

To keep reaching your Plex server once you're away from home, run
[Tailscale Funnel](https://tailscale.com/kb/1223/funnel) on the Plex host:

```
tailscale funnel --bg 32400
```

This exposes your Plex server at a stable public HTTPS URL (`https://<host>.<tailnet>.ts.net`)
that forwards straight to Plex's local port - set that hostname as `PLEX_FUNNEL_HOST` in
`secrets.h`. The device always tries `PLEX_LOCAL_IP` first (fast, no internet dependency at
home) and only falls back to the Funnel URL when the local address doesn't respond. Note this
exposes Plex's HTTP API to the public internet, gated only by your `PLEX_TOKEN` (same as Plex's
own API access control today) - treat the `*.ts.net` hostname as semi-secret.

Sessions/history come from Tautulli instead, so it needs the same treatment - run Funnel for its
port too (on whichever host runs Tautulli):

```
tailscale funnel --bg 8181
```

and set that hostname as `TAUTULLI_FUNNEL_HOST`. If Tautulli runs on the same host as Plex, that
host already has a funnel bound to port 32400/443 - Tailscale Funnel supports up to 3 concurrent
funnel ports per node, so Tautulli needs its own funnel port instead:

```
tailscale funnel --bg --https=8443 8181
```

with `TAUTULLI_FUNNEL_HOST` then including that port, e.g. `"nerdflix.cetacean-cloud.ts.net:8443"`
(this is exactly how nerdflix is set up: Plex on the default 443, Tautulli on 8443). See the
[Funnel docs](https://tailscale.com/kb/1223/funnel) for exact multi-port syntax. As with Plex,
this exposes Tautulli's API to the public internet gated only by `TAUTULLI_API_KEY` - treat that
hostname as semi-secret too.

## Touch calibration

The raw ADC-to-pixel mapping is set from one unit's actual corner-press readings
(`TOUCH_RAW_X_MIN/MAX`, `TOUCH_RAW_Y_MIN/MAX` in `src/main.cpp`). If your panel's range
differs enough that dragging feels off, watch the serial log while pressing each corner and
adjust those constants; if scrolling ever feels reversed on an axis, swap that axis's min
and max.

## Notes

- No PSRAM is assumed by default; the code detects PSRAM at boot and uses a larger cover-art
  buffer automatically if present.
- Polling interval is 3 seconds; the server-unreachable error screen appears after 2 consecutive
  failed Tautulli polls (~6s) to avoid flickering on a single transient blip.
- Uses both ESP32 cores: core 0 handles WiFi/Tautulli polling and cover-art JPEG decode, core 1
  runs the LVGL UI loop and touch input.
