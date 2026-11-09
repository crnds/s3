# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A PlatformIO firmware for the **JC3248W535EN** (Guition 3.5″, ESP32-S3 N16R8,
480×320 IPS, AXS15231B QSPI panel + capacitive touch) that runs the Claude Code
token-usage dashboard, **plus the Mac server it polls** (`server/`). It began
as a port of the 320×240 "Cheap Yellow Display" (CYD) dashboard; that board is
retired and its repo (`~/cyd`, history at github.com/crnds/cyd) was folded in
here on 2026-11-09. "The CYD" in comments below means that lineage.

```
Mac laptop                                   S3 board (on WiFi)
server/usage_server.py  ──/api/usage──▶      src/*.cpp (polls every 20s, 7 pages)
  :8787, launchd com.corner.s3usage           simulator-s3.html = browser twin
server/control_server.py (:8788, localhost; com.corner.s3control)
  ▲ POST /api/note (localhost): note.html (served by :8787), tools/note.py (CLI)
```

`esp32-s3-spec.md` is the board reference (pins, connectors, frame-rate
math). `design.md` is the design system for the dashboard UI (tokens,
components, touch, motion, migration plan) — follow it for any UI change.
`docs/` (seller package, factory backup) is local-only, gitignored.
Mac-side internals are in **"Mac server"** below. The browser pages:

- **`server.html`** — control panel (status, on/off, battery save, firmware
  OTA: build & publish, install on board, each board's build; logs),
  served by `server/control_server.py` at `http://127.0.0.1:8788/`
  (`HTML_PATH`, repo-relative).
- **`note.html`** — Note page editor, served by
  `server/usage_server.py` at `http://127.0.0.1:8787/` (`NOTE_HTML_PATH`).

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
- **Colours:** firmware RGB565 in `tokens.h`, simulator RGB888 with the
  `// 0x....` comment beside each. Change both together.
- **Two themes (Settings > Theme, NVS `light_mode`).** Theme-dependent
  colour roles are runtime tokens (`extern uint16_t` in `tokens.h`), assigned by
  `applyThemeTokens()` in `settings.cpp` (twin: the simulator's function of the
  same name); design.md 4.6 lists them. Never capture one in a `static const`, and check
  new UI in both themes. `loadThemeEarly()` reads the key before the first boot frame.
- **Deliberate non-twins:** speaker output (`src/audio.cpp`; the sim keeps the Settings > Sound
  rows and stays silent), cat GIF and movie (`.mjpeg`) playback (sim shows
  the placeholder layout with the real overlays), the AP setup screen, boot
  splash/spinner, and TE sync — firmware-only. Slides, sheets, scrims, fades
  and the backlight animator ARE twinned (the sim quantises scrims/fades
  through RGB565 too).
- After any UI change: screenshot the sim headless **and** the board
  (`tools/grab_screen.py`) and compare (see Commands).
- **Note tokenizer is a four-copy parity surface**: `src/pages.cpp`,
  `simulator-s3.html`, `note.html` and `tools/note.py`.
  `grep -rln noteWordColor src simulator-s3.html note.html` finds the first
  three; `note.py` holds its copy under `NOTE TOKENIZER + WRAP`. The S3 pane uses a monospace font so the wrap walk stays a
  cols×rows grid; every size holds at least the CYD pane's columns, so the
  CYD-geometry fit check in `note.html`/`note.py` stays a safe bound.

## Commands

```sh
PIO=pio                                  # Homebrew's /opt/homebrew/bin/pio (older setups: ~/.pio-venv/bin/pio)
$PIO run                                 # build (first run downloads pioarduino)
$PIO run -t upload                       # flash over native USB (port auto-detected)
$PIO run -t publish                      # build and offer it to the board over WiFi (OTA, below)
$PIO device monitor                      # serial log (baud is irrelevant on native USB)
python3 tools/size_check.py              # CI's size gate on the last build (--update after planned growth)
node tools/sim_check.mjs                 # CI's sim goldens (--update after a deliberate UI change)
python3 tools/grab_screen.py "x 1 g:status n 1 g:usage w 1 g:weather x"
                                         # drive the board over serial, save shots/*.ppm
python3 tools/make_vlw.py                # regenerate fonts (firmware + sim) after editing FONTS
```

**Port:** the board enumerates as `/dev/cu.usbmodemNNNN`, and the number
follows the Mac's USB socket (it has been `1101` and `1301`). So
`platformio.ini` sets no port: PlatformIO auto-detects the board (USB
Serial/JTAG, 303A:1001), and `grab_screen.py` uses the only
`/dev/cu.usbmodem*` present. Pass `--upload-port` / `--port` when several
are plugged in.

`grab_screen.py` needs pyserial: the system `python3` has it on this Mac
(older setups used `~/.pio-venv/bin/python`). It has no Pillow, so the script
falls back to `shots/<name>.ppm`; convert it with
`sips -s format png shots/x.ppm --out shots/x.png`. It can't open the port while
`pio device monitor` holds it. In its command string a bare number is a
pause in seconds, and `g:name` writes `shots/<name>.ppm` (`.png` with Pillow). A grab is a single
frame, so it can't show a one-frame glitch (flash, tear): those need eyes on
the panel.

Serial debug keys (main.cpp `serialCommand`): `n`/`p` next/prev page,
`w`/`d`/`s` Weather/Device/Settings, `x` close sheets, `h` play the hourly
signal now, `z` toggle screen
sleep, `m` slow motion (every motion token x10, design.md 17), `g` dump the frame
(`S3SHOT 480 320\n` + raw big-endian RGB565 + `\nS3END\n`). The simulator
canvas takes the same keys (plus arrows/Esc) when focused, and
`?slowmo=10` starts it in slow motion. A grab is the `frame` buffer, not the
composite on the panel, so grab after transitions settle.

Headless sim screenshot: `~/node_modules/playwright` is gone, so use the
Playwright MCP. It blocks `file://`, so serve the repo first
(`python3 -m http.server 8765`, then open `http://127.0.0.1:8765/simulator-s3.html`;
the usage server allows loopback origins, so it polls real data when that is
running). The sim needs no server: its
Connection card's Data menu defaults to "Server, else mock", which falls back to
the built-in `MOCK` document shifted to the current time (`mockDoc()`), and
`?data=mock` skips the fetch entirely (`?data=server` shows the real offline
path). It also runs opened straight from disk (`file://`).
The MCP only writes screenshots under `~/s3`, so save them to `.playwright-mcp/`
(untracked) and delete it afterwards. To set data and the page, `browser_evaluate`
`Object.assign(STATE, {currentPage: 0, btcPrice: 82893, ...}); render();`.
To compare pixel for pixel, read the canvas with `getImageData` in the same
evaluate rather than cropping a screenshot.
The `#screen` box is 486×326 = 480×320 canvas + 3px border; crop 3px to
compare against a board grab pixel-for-pixel.

**CI** (`.github/workflows/ci.yml`, every push): the `firmware` job builds
with `config.example.h` and runs `tools/size_check.py` -- fail past 75% of the
app slot, or past `tools/size_baseline.json` + 32KB (planned growth updates
the baseline in the same commit). The `simulator` job runs
`tools/sim_check.mjs`: `simulator-s3.html?fixture=default` (frozen clock and
data from `tests/fixtures/default.json`, no polling, `window.simFixture.show()`)
draws every page and sheet in both themes and compares each with
`tests/golden/default/*.png` (per-channel tolerance 2 for blending
differences between machines; failures leave actual + diff PNGs in
`tests/out/`). **Any visible UI change updates the goldens in the same
commit.** Playwright is pinned to 1.63.0 with no package.json: locally
`npm install --no-save playwright@1.63.0 && npx playwright install chromium`
(node_modules/ is gitignored), or point `NODE_PATH` at an install. Goldens
made on the Mac pass on Linux within the tolerance; if they ever drift, run
the workflow by hand with `update_goldens` and commit its artifact. Native
tests (O4) and the tokenizer corpus (O5) get CI steps when they exist.

A `src/config.h` must exist (copy `src/config.example.h`; gitignored).
Git remote is `origin` (github.com/crnds/s3). `docs/`, `shots/` and `config.h`
are gitignored (`cats/` is tracked: its GIFs are embedded in the firmware).

## Toolchain facts (verified)

- **Platform is pinned to pioarduino `51.03.07` = Arduino-ESP32 core 3.0.7
  (IDF 5.1)** — the generation the seller's AXS15231B driver targets. The stock
  `espressif32` 6.x platform (core 2.0.x / IDF 4.4) has no QSPI `esp_lcd` and
  will not build this.
- Flash 16MB, `default_16MB.csv` (6.25MB app ×2). OPI PSRAM (`qio_opi`), 8MB.
  Build is ~2.97MB (45% of the app slot): ~1.3MB of it is the embedded cat
  GIFs and ~200KB is fonts.
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
  redraw), page slide (`displayPresentSlide`), sheet rise/drop with the
  RGB565 shift-and-mask scrim (`displayPresentSheet`) and the Reduce Motion
  cross-fade (`displayPresentFade`) -- each a two-frame composite with its own
  inner loop in `fillStrip`. Rotation NORMAL/FLIPPED = software 90°/270° in
  the rotate copy and in `touch_axs.cpp`'s inverse map. There is no border
  flash any more (design.md 12.8). The hourly signal is a panel inversion:
  `displaySetInvert()` writes INVON/INVOFF straight from `loop()` (no present,
  so sheets and static screens flash too) on the even seconds of hh:00:00..05;
  serial `h` plays it now. `motion.cpp`'s backlight animator fades sleep, wake,
  Night Mode edges and brightness changes (its breath is currently unused).
- **Navigation (`nav.cpp`, twin: the simulator's NAV block).** The touch
  router, gesture recogniser and every transition, stepped once per `loop()`
  pass -- nothing blocks, so a touch during motion is always read. Commit on
  touch-up with a 10px slop and a 120ms rearm from the last up; pressed states
  on the down. Carousel: the page halves (the 8px lean is the pressed state),
  plus horizontal swipe. Sheets (Weather, Device Stats, Settings) rise from the
  bottom over a stepped scrim and dismiss by close glyph, tap-anywhere
  (read-only sheets) or drag down; Settings pushes details from the right and
  swipe-right pops; the list scrolls 1:1 with momentum and rubber-banding.
  Springs, velocity handoff and rubber-band are `motion.cpp`. Only one
  transition runs at a time; while one is up, `presentFrame()` just marks the
  frame dirty and `navTick()` composites it. Buffers: `prevFrame` (outgoing
  screen / dropping sheet) and `behindFrame` (the page under an open sheet),
  300KB each in PSRAM. A `[motion]` serial line per transition reports frames,
  the slowest present (~23ms slide, ~30ms sheet, ~12 frames) and the longest
  gap between composites, counted from the slide's start so it includes
  composing the incoming page (~65ms onto a media page).
- **Media pages load after the slide lands.** A slide onto GIF/MOVIE/MIXED/
  OVERVIEW never opens or decodes: `preparePage()` stages the page with
  `gifPlayerStagePage()` (the page's poster -- its last screen, captured from
  `prevFrame` as a slide leaves it, four lazily allocated 300KB PSRAM buffers --
  or the plate before a first visit, plus the overlays; a pane page's static
  half is redrawn live), and `loop()` skips the whole cat-mode branch while
  `navPageTransitionActive()`. The first pass after landing opens a fresh clip
  and decodes it in one tick. A theme change invalidates the posters
  (`navInvalidatePosters()`). A first-frame decode in the slide cost the movie
  slide 5-9 frames and a ~230-330ms stall.
- **Presents are batched per loop pass.** `loop()` (~33ms period) collects
  "changed" from `gifTick()` and, only on the shared 8Hz ambient tick
  (`TOK_MOTION_AMBIENT_TICK_MS`), from `shineTick()` (pace sweep every 3s),
  `pulseTick()` (status-dot breath) and `progressTick()` (hairline) -- all draw
  straight into `frame` -- and presents once. `render()` (1Hz on normal pages)
  composes and presents itself and restarts the ambient tick. **Every present
  is a whole ~16ms frame**, so presents/s is the idle CPU budget: ambient ticks
  at the loop rate put the status page at 52% CPU (8Hz: 16%). The 10s
  `[timing]` serial line reports `presents N/s`; page 0 at rest is ~7.8.
  `presentHold` suppresses presents while nav composes an incoming screen
  off-screen. Drawing cost is dominated by PSRAM bandwidth (full clear ~8ms)
  and anti-aliased text (~1ms per string); the status page is ~54ms at 1Hz.
  Smooth shapes use `aaFillRoundRect` / `aaFillCircle` / `aaRing` (pages.cpp:
  LovyanGFX's algorithm blended straight into the buffer).
- **Two cores, two locks — unchanged from the CYD:**
  all blocking I/O on `networkTask` (core 0); `loop()` on core 1 only touches
  and renders; `stateMutex` held by `render()` while drawing; `sdMutex` →
  `stateMutex` is the only allowed nesting; the GIF player uses
  `tryLockSD(20)` and drops a frame rather than stall a recovery poll.
- **No TLS on the board** — BTC/weather/AQI still arrive inside `/api/usage`.
  With 8MB PSRAM the old (CYD) heap reason is gone, but the Mac's
  battery-aware fetch cadence lives server-side, so it stays.
- **Touch (`touch_axs.cpp`):** I2C 0x3B, 11-byte read command, 8-byte report,
  single point; 2 fingers = no touch (seller driver behaviour). Capacitive —
  **no calibration keys** exist (the CYD's `touch_*` NVS keys were dropped).
- **Cats (`gif_player.cpp`):** the GIFs are **linked into the firmware**, not read
  from the SD card: `tools/embed_cats.py` (a `pre:` extra script) `.incbin`s
  `cats/*.gif` into `src/cats_embed.gen.cpp` (gitignored) and AnimatedGIF decodes
  straight from memory-mapped flash, so cats need no card and no `sdMutex`
  (`cats_embed.h` is the interface). RAW-mode decode composited into `gifCanvas`
  (GIF-sized RGB565 in PSRAM), then blitted into `frame` — 1:1 centred on the
  full-screen cat page, or **cover-fit** (uniform scale computed once per GIF
  open, cropped to fill — never stretched) into a pane: the mixed page's 328px
  right column (nearest-sampled) or the Overview's 150x89 cat cell (below half
  scale, the mean of four samples per pixel, or it shimmers). Compositing on a
  GIF-owned canvas is what keeps transparency/disposal right under the resize
  and the overlays. Frame delays
  are honoured
  as-is (no 12fps ceiling). A layout flip under an open GIF (going
  offline/online on a pane page) reopens it for the new layout.
  **Never present between a close and the next decoded frame.** Opening
  clears the canvas/pane to black, so a reopen (loop end, next cat, layout
  flip) is deferred via `nextOpenIsLoop` and done in the same `gifTick()` as
  the first frame's decode. The last frame is held for its own delay first.
  Presenting in between is the one-frame black flash users see at a loop.
  Movies read the SD in 8KB blocks (`readNextFrame`; the overshoot past a
  frame's FFD9 is carried to the next frame, still no `seek()`): 6.5 -> 9.6 fps.
- **Pane pages (Mixed, Overview; `catPanePage()` / `catPaneRect()`,
  pages.cpp).** Media pages with a static half: `render()` returns early for
  them, and `loop()` redraws `drawMediaPageStatic()` (everything but the pane,
  plus the strip) once a second around the playing GIF. The Overview
  (design.md 7.5) is a 3x3 grid of the cards in miniature; its weather cell
  opens the Weather sheet. Offline both show the full-screen cat.
- **OTA (`ota.cpp`, roadmap O9).** `pio run -t publish` (`tools/ota_publish.py`)
  copies the build to `~/.s3_firmware/s3.bin`; `server/usage_server.py` announces
  it in `/api/usage` as `firmware.s3.{sha256,size}` and serves it at
  `/firmware/s3.bin` (to this Mac and to boards that poll it). The sha256 is the
  image digest esptool appends -- what `esp_partition_get_sha256()` returns for
  the running app -- so offered == running means up to date, no version
  numbers. networkTask downloads into the idle slot of `default_16MB.csv`,
  checks the slot holds the announced digest, and restarts. A new build boots
  `PENDING_VERIFY` and is confirmed only by its first good live poll
  (`extern "C" verifyRollbackLater()` returns true -- a C++-mangled definition
  silently leaves the core's weak one, which confirms at boot); a crash before
  that, or 10 min without a good poll, and the bootloader rolls back. NVS
  `s3ota`: `seen` (auto update installs only a build it hasn't acted on, so a
  dev build flashed over USB isn't replaced by an older publish -- that shows
  "Update ready" instead), `try` (until confirmed), `bad` (rolled back: auto
  skips it), `req` (the last control-panel install request acted on). Every
  poll sends `X-S3-Firmware: <running digest>` and `X-S3-OTA: <state>;
  auto=<0|1>; req=<id>`, which `server.html`'s Firmware panel shows; its
  "Install on board" puts `firmware.s3.install: <id>` in the offer, which the
  board acts on once per id whatever Auto update says (and even for a build it
  skipped). A download that stalls is retried twice. Settings > Firmware: the
  state summary, Auto update (`ota_auto`, default on) and Update now. Serial `[ota]` lines and `/diag_log.csv`
  `ota_*` events trace each step. USB flashing always still works.
- **Fonts (`fonts.cpp`):** VLW preloaded once into `lgfx::VLWfont` objects;
  `drawText/drawTextR/drawTextC(FontId, …)` are the only text API — no
  `setTextSize`/`print` built-in font anywhere. ASCII only (server
  `sanitize_note` already folds the rest to `?`). Digits are tabular (`tnum`).
- **SD is `SD_MMC` 1-bit** (CLK12/CMD11/D0 13), 40MHz with a 20MHz fallback.
  Same files as the CYD (`/last_usage.json`, `/last_env.json`,
  `/weather.json`, `/archive.csv`, `/diag_log.csv`, `/splash.bmp` now
  480×320). `/movies/*.mjpeg` is still SD-only; cats are not.
- **NVS namespace `s3cfg`** (separate board, separate settings). Same keys as
  the CYD minus touch calibration; no `/config.json` migration.
- **Offline threshold / WiFi self-reboot are wall-clock** (max(3 min, 3 poll
  intervals) — `offlineAfterMs()` / 15 min), never poll-cycle counts: the
  CYD's old `OFFLINE_AFTER_CYCLES = 3` became a 15-second hair trigger at the
  5s poll setting and made the board flap OFFLINE on ordinary packet loss. The simulator's
  `offlineAfterMs()` mirrors it.
- **Stale data (`dataStale()`, pages.cpp; design.md 13.4):** before offline,
  once the last good *live* poll (`lastFetchSuccessMs`, 0 = none since boot,
  i.e. the SD cache) is older than max(2 poll intervals, 1 min), the strip's
  CPU readout becomes "Updated Nm ago" / "Saved data" and the limit numerals
  go `text.secondary`. Twinned in the simulator.
- **Sound alerts (`pollAlerts`, main.cpp; design.md 12.10):** networkTask
  reads them off each poll and calls `audioPlay()` (safe from any core; it
  applies the switches, volume and mute rules). Alerts = a limit projected to
  run out before its reset (`paceSecondsToLimit`, shared with the pace flag,
  once 10% of the window has passed), a limit reaching 100%, going offline;
  each once per window/outage. Claude ding = the server's `active_now`
  (`STATE.claudeActive`, live polls only) falling true -> false. The first
  good poll after boot or an outage only primes them. `SND_ATTN` has no
  trigger yet (needs Claude Code hooks on the Mac, roadmap.md F1).
- **Screen sleep (`enterScreenSleep`/`exitScreenSleep`, main.cpp):** the
  sleep icon button (a minimal grey pill in corner slot a, `drawSystemCorner`,
  `SLEEP_HIT_*` in state.h) is drawn last on every screen, and its hit box is
  checked before every other touch target. Corner slot b (the Battery Save
  glyph) sits to its left; content ink stays 8px clear of both. Sleeping
  fades the backlight to 0, sends panel DISPOFF + SLPIN (`displaySetSleep`), drops the
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

## Mac server (`server/`)

Two launchd jobs, plists in `server/`, installed copies in
`~/Library/LaunchAgents/` (editing the repo copy does nothing until it is
re-copied and `launchctl kickstart -k gui/$UID/<label>`):

| Label | Process | Port | Logs |
|---|---|---|---|
| `com.corner.s3usage` | `server/usage_server.py` | `*:8787` (LAN: the board polls it) | `/tmp/s3usage.{log,err}` |
| `com.corner.s3control` | `server/control_server.py` | `127.0.0.1:8788` only | `/tmp/s3control.{log,err}` |

```sh
/usr/bin/python3 server/usage_server.py          # run by hand (stop the agent first)
curl -s localhost:8787/api/usage | python3 -m json.tool
launchctl kickstart -k gui/$UID/com.corner.s3usage   # restart after editing
tail -f /tmp/s3usage.log                          # one line per board poll = heartbeat
tools/note.py "buy milk"                          # set the Note page from a terminal
```

Local state outside the repo: `~/.s3_note.json` (the note), `~/.s3_firmware/`
(published OTA builds; outside the repo because an image carries the WiFi
credentials). Optional tokens in `secrets.local.json` at the repo root
(gitignored): `AQICN_TOKEN`, `GIPHY_API_KEY`; env vars of the same name win.

- **Stdlib only**, run by launchd's **`/usr/bin/python3` (3.9.6)** — keep it
  3.9-compatible (e.g. `datetime.fromisoformat` can't parse a trailing `Z`,
  hence `.replace("Z","+00:00")`). Test with `/usr/bin/python3`.
- **Background loops** (each parks after ~180s with no client polls via
  `activity_gated_loop`; first iteration always runs):
  1. `scan_loop` (~15s AC / ~30s battery) — tails `~/.claude/projects/*/*.jsonl`
     by byte offset into 5-min buckets (8-day retention). Streamed assistant
     messages repeat with identical usage, so events are **deduped on
     `(message.id, requestId)`, keep-first** — otherwise totals ~double.
  2. `limits_loop` (~120s AC / ~300s battery) — OAuth
     `api.anthropic.com/api/oauth/usage` session/week percents + resets. Token
     read via `/usr/bin/security` (Keychain); never written to disk or
     returned. Last-good kept on failure; 429 backoff clamped 10–30 min.
  3. `market_loop` — BTC (Binance, ~60s AC / ~180s battery), Bangkok weather
     (Open-Meteo, 10 / 30 min), Bangkok AQI (15 / 30 min): aqicn.org station
     data when `AQICN_TOKEN` is set, else Open-Meteo's keyless CAMS `us_aqi`
     (same EPA 0–500 scale; logged once). Last-good kept on failure. All TLS
     stays on the Mac — the firmware has no `WiFiClientSecure`.
- **`battery_guard_loop` owns the HTTP socket.** The 20s poll cadence never
  leaves macOS an idle gap long enough to sleep (a night on battery once
  drained 100%→1%). On battery below `BATTERY_LOW_PCT` (50%) it closes the
  listening socket so the Mac can sleep (the board goes OFFLINE → cats), and
  rebinds on AC or recovery.
- **Battery save** (socket still up, Mac unplugged, or forced): background
  intervals stretch as above; `power.battery_save` true. Override with
  `POST /api/battery-save` `{"enabled": true|false|null}` (`null` = auto),
  localhost, no `Origin`. The board's Settings > Battery Save AUTO follows it
  from live polls only.
- **`GET /api/usage` is the contract** with `src/net.cpp` and the simulator —
  renaming a field means editing all three. Keys: `today`/`week`
  `{tokens,cost}`, `active_now`, `projects[{name,tokens}]`, `trend[7]`,
  `limits{session,week,week_model,credits}` (null until the keychain read
  works; `resets_in_sec` computed per request), `context{tokens,percent}`,
  `btc{price,changePct}`, `weather{...,hourly[],daily[]}`, `aqi{aqi}` (each
  null until its first fetch — clients keep last-known when absent),
  `note{text,size}` (**always an object**: clearing it must clear the board),
  `power{...}`, `clients[]`, `firmware{s3:{sha256,size,published,install?}}`,
  `generated_at`, `epoch`. New fields are additive and optional.
- **Note routes, localhost-only** (`_is_local()`, LAN gets 403): `GET /`,
  `/note`, `/note.html` serve `note.html` fresh per request; `GET|POST
  /api/note` (`{"text", "size": 1-3}` → `sanitize_note()` → atomic write to
  `~/.s3_note.json`). POST `Origin` is checked against `NOTE_ALLOWED_ORIGINS`
  (note.html sends one; `note.py`/curl send none). `sanitize_note()` folds
  CRLF→LF, tab→space, non-printable-ASCII → `?`, caps at 480 chars. The board
  reads the note over the LAN inside `/api/usage`.
- **OTA**: see Firmware internals › OTA. Server side: `firmware_offer()` reads
  `~/.s3_firmware/s3.bin`; `GET /firmware/s3.bin` only to this Mac and IPs
  that have polled; the board reports `X-S3-Firmware` / `X-S3-OTA` per poll
  (`note_board_firmware`); localhost `GET /api/firmware` and `POST
  /api/firmware/install` back the control panel (an install request rides in
  the offer for ≤15 min).
- **CORS is an exact allowlist, never `*`**: `Origin: null` (file://) on all
  routes; loopback origins on any port (`USAGE_CORS_ORIGIN_RE`, e.g. the sim
  under `python3 -m http.server 8765`) on `GET /api/usage` only.
- **Control server** is a separate process so the panel stays up while the
  usage server is disabled: `GET /api/status`, `POST /api/enable|disable`
  (launchctl bootstrap/bootout of `com.corner.s3usage`), `POST
  /api/battery-save` (proxy), `POST /api/firmware/publish` (`pio run -t
  publish` in this repo, one at a time, 15-min cap, last 200 lines kept; pio
  found on PATH or `PIO_CANDIDATES`), `POST /api/firmware/install` (proxy).
  **Localhost-bound on purpose** — it can run launchctl.
- Dollar costs are estimates from a hardcoded per-model price table.

## Layout grid and tokens

`design.md` section 7 is the source of truth: 8px margins and gutters, content
x 8..471 / y 8..291, a fixed 128px left column (x 8..135) and a 328px right
column (x 144..471; the Usage page and Weather sheet keep an alignment line at
x 188), status strip y 300..318, progress hairline y 319. The strip's two
targets are 56x40, reaching up to y 280 (design.md 9.6). Page navigation
splits at the screen half (x 240), independent of the grid.
Colours, type roles, spacing, radii, touch and motion values are design
tokens in `src/tokens.h` (`TOK_*`, the upper snake of design.md's dotted
names), mirrored line for line in the simulator's TOKENS block (RGB888 with
the `// 0x....` beside). Screens use tokens only -- no raw colour, font id or
magic spacing. Shared hit boxes and card boxes live in `state.h`'s LAYOUT
block and are copied at the top of `simulator-s3.html`.

## Cat library

Cats live in `~/s3/cats/*.gif` (tracked) and are **embedded in the firmware** at
build time -- add or remove a GIF, then `pio run -t upload`. Flash is 16MB with a
6.25MB app slot (~1.6MB is code+fonts), so `embed_cats.py` fails the build past
4MB of GIFs; that is why a full 120-cat library (~83MB after fitting) does
not go in -- pick a subset.

```sh
cp cats_raw/cat_NNN.gif cats/                  # pick from a tools/prepare_cat_gifs.py library
python3 tools/fit_cats.py                      # enlarge sources smaller than 480x320
python3 tools/embed_cats.py                    # (optional) preview what gets embedded
```
`fit_cats.py` enlarges each until it touches 480x320 (Catmull-Rom, same
palette/lossy settings; needs `gifsicle`). Hidden files (macOS `._*.gif`) are
skipped.
