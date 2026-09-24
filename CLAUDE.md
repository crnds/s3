# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A PlatformIO firmware for the **JC3248W535EN** (Guition 3.5″, ESP32-S3 N16R8,
480×320 IPS, AXS15231B QSPI panel + capacitive touch) that runs the Claude Code
token-usage dashboard. It is a **port of `~/cyd`** (the 320×240 CYD version):
same Mac server, same `/api/usage` JSON contract, same pages/settings/offline
behaviour, re-laid out for the bigger panel with anti-aliased fonts, a
TE-synced full-frame present, native-rate cat GIFs and page slides.

```
Mac laptop                                   S3 board (on WiFi)
~/cyd/server/usage_server.py ──/api/usage──▶ src/*.cpp (polls every 20s, 6 pages)
(unchanged; the S3 is just a second client)   simulator-s3.html = browser twin
```

`esp32-s3-spec.md` is the board reference (pins, connectors, frame-rate
math). `design.md` is the design system for the dashboard UI (tokens,
components, touch, motion, migration plan) — follow it for any UI change.
`docs/` (seller package, factory backup) is local-only, gitignored.
The Mac side lives in `~/cyd` and is **not** duplicated here — for server,
`note.py` and control-panel internals read `~/cyd/CLAUDE.md`. The two
exceptions are the browser pages, which live here while their servers stay
in `~/cyd` and serve them by absolute path:

- **`server.html`** — control panel (status, on/off, battery save, logs),
  served by `~/cyd/server/control_server.py` at `http://127.0.0.1:8788/`
  (`HTML_PATH`).
- **`note.html`** — Note page editor, served by
  `~/cyd/server/usage_server.py` at `http://127.0.0.1:8787/` (`NOTE_HTML_PATH`).

Both follow the `~/CLAUDE.md` static-web conventions and are **not** bound
by the parity rule (except `note.html`'s tokenizer, below). Both are read
fresh per request, so edits show up on reload with no restart.

## The one invariant that matters most

`src/pages.cpp` + `src/settings.cpp` and `simulator-s3.html` render the **same
UI** and must be kept in lockstep: same coordinates, fonts, colours, text.

- **Text parity is structural, not approximate.** `tools/make_vlw.py` renders
  every font once and writes both the firmware's VLW arrays
  (`src/fonts/fonts_data.h`) and the simulator's glyph atlas (`sim/fonts.js`).
  Both sides position text by the same two rules (`src/fonts.h`): `y` is the
  line top (glyph top = `y + maxAscent − dY`), and width = sum of advances.
  Never swap the simulator to browser `fillText` — that breaks line-fit
  predictions, the whole point of the sim (the S3 version of the CYD's
  "6px per character" rule).
- **Colours:** firmware RGB565 in `state.h`, simulator RGB888 with the
  `// 0x....` comment beside each. Change both together.
- **Deliberate non-twins:** cat GIF playback (sim shows the placeholder layout
  only), the AP setup screen, boot splash/spinner, and the present-time
  effects (TE sync, border flash) — firmware-only.
- After any UI change: screenshot the sim headless **and** the board
  (`tools/grab_screen.py`) and compare (see Commands).
- **Note tokenizer is a six-copy parity surface**: `src/pages.cpp`,
  `simulator-s3.html`, `note.html`, and `~/cyd`'s `pages.cpp`, `simulator.html`,
  `note.py`. `grep -rn noteWordColor ~/s3/src ~/s3/simulator-s3.html ~/s3/note.html ~/cyd`
  finds them. The S3 pane uses a monospace font so the wrap walk stays a
  cols×rows grid; every size holds at least the CYD pane's columns, so the
  CYD-geometry fit check in `note.html`/`note.py` stays a safe bound.

## Commands

```sh
pio run                                  # build (first run downloads pioarduino)
pio run -t upload                        # flash over native USB (/dev/cu.usbmodem1101)
pio device monitor                       # serial log (baud is irrelevant on native USB)
python3 tools/grab_screen.py "x 1 g:status n 1 g:projects w 1 g:weather x"
                                         # drive the board over serial, save shots/*.png
python3 tools/make_vlw.py                # regenerate fonts (firmware + sim) after editing FONTS
```

`grab_screen.py` must run under the **system `python3`** (it has pyserial +
Pillow; PlatformIO's venv has no Pillow), and it can't open the port while
`pio device monitor` holds it. In its command string a bare number is a
pause in seconds, and `g:name` writes `shots/<name>.png`. A grab is a single
frame, so it can't show a one-frame glitch (flash, tear): those need eyes on
the panel.

Serial debug keys (main.cpp `serialCommand`): `n`/`p` next/prev page,
`w`/`d`/`s` Weather/Device/Settings, `x` close overlays, `z` toggle screen
sleep, `g` dump the frame
(`S3SHOT 480 320\n` + raw big-endian RGB565 + `\nS3END\n`). The simulator
canvas takes the same keys (plus arrows/Esc) when focused.

Headless sim screenshot (Playwright is installed at `~/node_modules`):
```sh
node -e 'const{chromium}=require(require("os").homedir()+"/node_modules/playwright");(async()=>{const b=await chromium.launch();const p=await b.newPage();p.on("pageerror",e=>console.log("ERR",e.message));await p.goto("file://'"$PWD"'/simulator-s3.html");await p.waitForTimeout(1500);await p.locator("#screen").screenshot({path:"/tmp/s3sim.png"});await b.close();})()'
```
The `#screen` box is 486×326 = 480×320 canvas + 3px border; crop 3px to
compare against a board grab pixel-for-pixel.

A `src/config.h` must exist (copy `src/config.example.h`; gitignored).
Git is local only (no remote). `docs/`, `cats/`, `shots/` and `config.h`
are gitignored.

## Toolchain facts (verified)

- **Platform is pinned to pioarduino `51.03.07` = Arduino-ESP32 core 3.0.7
  (IDF 5.1)** — the generation the seller's AXS15231B driver targets. The stock
  `espressif32` 6.x platform (core 2.0.x / IDF 4.4) has no QSPI `esp_lcd` and
  will not build this.
- Flash 16MB, `default_16MB.csv` (6.25MB app ×2). OPI PSRAM (`qio_opi`), 8MB.
  Build is ~1.55MB (24% of the app slot); ~200KB of it is fonts.
- Native USB Serial/JTAG — uploads at 921600 are reliable here (unlike the
  CYD's CH340, which needed 115200). If the port vanishes: hold BOOT, tap RST.
- A first `pio run` can die in pioarduino's package postinstall
  (`FileNotFoundError: package-postinstall.py`); re-running it succeeds.
- `lib/axs15231b/esp_lcd_axs15231b.h` includes `esp_lcd_panel_io.h` instead
  of the seller's `hal/spi_ll.h`: the latter clashes with C++ linkage
  (`conflicting declaration of C function`). Keep that if you re-vendor the
  driver.
- Factory image backup: `docs/backups-factory-firmware/` (see spec for restore).

## Firmware internals

- **Graphics pipeline (`display.cpp`).** Every page draws into `frame`, a
  480×320 16bpp `LGFX_Sprite` in PSRAM (`g` points at it). LovyanGFX has no
  AXS15231B driver, so it is a *canvas only*; the panel side is the seller's
  `esp_lcd_axs15231b` (vendored, panel half only, in `lib/axs15231b/`).
  **In QSPI mode the panel can't address partial windows** — the driver sends
  CASET but no RASET, and a write at y≠0 is `RAMWRC` (continue) — so every
  update is a whole frame streamed from row 0. `displayPresent()` waits for
  the TE falling edge (GPIO38, ~60.4Hz measured), rotates the landscape frame
  into two ping-pong 320×48 internal-DMA strips, and streams 10 strips.
  Measured: ~16ms fill+transfer, ~30ms including the TE wait. The CYD's
  dirty-band partial pushes, right-edge mask and 60MHz tearing hack are gone.
- **Present-time effects, not frame edits:** pixel-shift orbit (source offset
  + bg margin, in the rotate copy — a shift step is a re-present, never a
  redraw), hourly flash (panel `INVON`), touch flash (border overlay), page
  slide (two-frame composite, `displayPresentSlide`). Rotation NORMAL/FLIPPED
  = software 90°/270° in the rotate copy and in `touch_axs.cpp`'s inverse map.
- **Presents are batched per loop pass.** `loop()` (~33ms period) collects
  "changed" from `gifTick()`, `shineTick()`, `progressTick()` (all draw
  straight into `frame`) and presents once. `render()` (1Hz on normal pages)
  composes and presents itself. `presentHold` suppresses presents while a
  page slide composes the incoming page off-screen (`goToPage` in main.cpp).
- **Two cores, two locks — unchanged from the CYD** (read `~/cyd/CLAUDE.md`):
  all blocking I/O on `networkTask` (core 0); `loop()` on core 1 only touches
  and renders; `stateMutex` held by `render()` while drawing; `sdMutex` →
  `stateMutex` is the only allowed nesting; the GIF player uses
  `tryLockSD(20)` and drops a frame rather than stall a recovery poll.
- **No TLS on the board** — BTC/weather/AQI still arrive inside `/api/usage`.
  With 8MB PSRAM the old heap reason is gone, but the contract (and the Mac's
  battery-aware fetch cadence) is shared with the CYD, so it stays.
- **Touch (`touch_axs.cpp`):** I2C 0x3B, 11-byte read command, 8-byte report,
  single point; 2 fingers = no touch (seller driver behaviour). Capacitive —
  **no calibration keys** exist (the CYD's `touch_*` NVS keys were dropped).
- **Cats (`gif_player.cpp`):** RAW-mode decode composited into `gifCanvas`
  (GIF-sized RGB565 in PSRAM), then blitted into `frame` — 1:1 centred on the
  full-screen cat page, or **cover-fit** (nearest-sampled, uniform scale
  computed once per GIF open, cropped to fill — never stretched) into the
  mixed page's 240px pane. Compositing on a GIF-owned canvas is what keeps
  transparency/disposal right under the resize and the overlays. Frame delays
  are honoured
  as-is (no 12fps ceiling). A layout flip under an open GIF (going
  offline/online on the mixed page) reopens it for the new layout.
  **Never present between a close and the next decoded frame.** Opening
  clears the canvas/pane to black, so a reopen (loop end, next cat, layout
  flip) is deferred via `nextOpenIsLoop` and done in the same `gifTick()` as
  the first frame's decode. The last frame is held for its own delay first.
  Presenting in between is the one-frame black flash users see at a loop.
- **Fonts (`fonts.cpp`):** VLW preloaded once into `lgfx::VLWfont` objects;
  `drawText/drawTextR/drawTextC(FontId, …)` are the only text API — no
  `setTextSize`/`print` built-in font anywhere. ASCII only (server
  `sanitize_note` already folds the rest to `?`). Digits are tabular (`tnum`).
- **SD is `SD_MMC` 1-bit** (CLK12/CMD11/D0 13), 40MHz with a 20MHz fallback.
  Same files as the CYD (`/last_usage.json`, `/last_env.json`,
  `/weather.json`, `/archive.csv`, `/diag_log.csv`, `/splash.bmp` now
  480×320, `/cats/*.gif`).
- **NVS namespace `s3cfg`** (separate board, separate settings). Same keys as
  the CYD minus touch calibration; no `/config.json` migration.
- **Offline threshold / WiFi self-reboot are wall-clock** (60s / 15 min),
  never poll-cycle counts — see `~/cyd/CLAUDE.md` for the flap this fixed.
- **Screen sleep (`enterScreenSleep`/`exitScreenSleep`, main.cpp):** the
  solid grey pill (`drawSleepButton`, `SLEEP_BTN_*`/`SLEEP_HIT_*` in state.h) is
  drawn last on every screen, and its hit box is checked before every other
  touch target. Anything else in the top-right sits to its left: the Battery
  Save icon, the Weather AQI badge, the Settings title. Sleeping sets the
  backlight to 0, sends panel DISPOFF + SLPIN (`displaySetSleep`), drops the
  CPU to 80MHz, turns on WiFi modem sleep, and floors polls to
  `BATTERY_SAVE_POLL_SEC`. `loop()` then only reads touch every 50ms. The
  next press anywhere wakes the screen (after a 600ms guard) and is
  swallowed. Nothing is presented while asleep, so the frame and any GIF
  canvas are intact on wake. `applyEffectiveBrightness()` holds 0 while
  asleep. Touch shares the AXS15231B with the panel but keeps reporting under
  DISPOFF + SLPIN (verified on the board), which is what makes tap-to-wake
  possible.
- Device Stats reports internal SRAM and PSRAM separately (the S3's two
  pools) instead of the CYD's single DRAM constant; CPU% is the render
  loop's duty cycle with TE-wait time subtracted.

## Layout grid

3px outer margins, 2px card gaps. Left column x 3..238, right x 241..476,
content y 3..289, footer 292..319, progress line y=319, page-nav split x=240.
Hit boxes and every other shared constant live in `state.h`'s LAYOUT block
and are copied at the top of `simulator-s3.html`.

## Cat library

```sh
cp -R ~/cyd/cats ~/s3/cats                    # the CYD's 120 cats, same library
python3 tools/fit_cats.py                      # enlarge sources smaller than 480x320
COPYFILE_DISABLE=1 cp -r cats /Volumes/<SD>/cats
```
The S3 plays **the CYD's own 120 cats** (`~/cyd/cats`, built there by
`prepare_cat_gifs.py` at <=320x240) -- one shared library, not a second
download. `fit_cats.py` enlarges each until it touches 480x320 (Catmull-Rom,
same palette/lossy settings); the result is ~83MB. Grow the library in
`~/cyd` and re-copy + re-fit rather than downloading separately here.
`COPYFILE_DISABLE=1` stops macOS AppleDouble `._*.gif` files (the scanner
skips them anyway).
