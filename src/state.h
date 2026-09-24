// Shared declarations for the S3 dashboard firmware (JC3248W535EN port of
// ~/cyd's CYD dashboard; the CYD's own split of a 2920-line .ino survives here). Every .cpp file in this
// sketch includes this header; it is the only place cross-file globals,
// mutexes, and function prototypes are declared. Definitions live in exactly
// one .cpp each (see the comment above each block below for where).
//
// This is a PlatformIO build: there is no .ino and no Arduino auto-prototype
// pass at all, so every cross-file function and global must be declared here
// -- skipping one causes a link error in a different file, not a compile
// error in this one.
#pragma once

#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD_MMC.h>
#include <AnimatedGIF.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include <time.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <Preferences.h>

#include "pins.h"
#include "config.h"
#include "display.h"
#include "touch_axs.h"
#include "fonts.h"

// ── DISPLAY ────────────────────────────────────────────────
// Every page draws into `frame`, a 480x320 16bpp LGFX_Sprite in PSRAM (via
// `g`, kept as a pointer so the CYD's `g->` draw code ports unchanged), and
// presentFrame() hands the finished frame to display.cpp, which rotates it
// onto the portrait AXS15231B panel. There is no LovyanGFX panel device at
// all: the panel only takes whole frames (see display.h), so there is nothing
// to draw on "directly" -- every visible change goes through presentFrame().
// Defined in main.cpp.
extern LGFX_Sprite frame;
extern lgfx::LovyanGFX* g;

// Push `frame` to the panel (pixel shift, hourly-flash inversion, and the
// touch-flash border applied on the way). No-op while presentHold is set --
// the page-slide transition renders the incoming page off-screen first.
void presentFrame();
extern bool presentHold;
// Running total of time presents spent idle waiting for the panel's TE edge
// (main.cpp subtracts it from the CPU duty-cycle estimate).
extern volatile uint32_t teWaitAccumUs;
// Page-change slide: call pageTransitionBegin() BEFORE drawing the new page
// (snapshots the outgoing frame and holds presents), then draw the new page
// into `frame` the normal way, then pageTransitionRun() animates old -> new.
void pageTransitionBegin();
void pageTransitionRun(bool forward);
void flashTouchCenter();
bool pixelShiftTick(uint32_t now);
bool checkHourlyFlash(bool& isEvenSecond);
// Between-render animation top-ups (shine sweep, poll-progress line). Both
// draw straight into `frame`; they return true when they changed pixels, and
// loop() then presents once for the pass.
bool shineTick(uint32_t nowMs);
bool progressTick(uint32_t nowMs);

// Non-const: overridable from flash (see sd_store.cpp's
// loadRuntimeConfig). Originally set once at boot before the two tasks
// start; also settable at runtime from the Settings page's Poll Interval
// leaf (loop(), core 1), so it's volatile -- networkTask (core 0) reads it
// every cycle. Defined in main.cpp.
//
// cfgPollIntervalSec is the user's chosen rate (what the Poll Interval leaf
// shows/saves). POLL_INTERVAL_MS is the *effective* interval networkTask
// uses — Battery Save may stretch it via applyEffectivePoll().
extern volatile uint32_t cfgPollIntervalSec;
extern volatile uint32_t POLL_INTERVAL_MS;

// ── SHARED CONSTANTS ───────────────────────────────────────
// Internal linkage per TU (C++ global `const` default) — safe to define
// identically in every file that includes this header; no ODR issue.
const uint32_t TOUCH_DEBOUNCE_MS = 350;
const int PAGE_COUNT = 6;
// cfgBootPage sentinel: resume whichever page was on screen before the last
// restart (cfgLastPage), rather than a fixed page. See the Boot Page setting.
const int BOOT_PAGE_AUTO = -1;
const int GIF_PAGE = 3;    // 4th page (0-indexed): random cat GIFs from /cats/ on SD
const int MIXED_PAGE = 4;  // 5th page: status + cats split
// 6th page: the same left column as MIXED_PAGE, but the right half shows the
// note text from note.html instead of cats. Unlike GIF_PAGE/MIXED_PAGE this is
// an ordinary render() page (a `case` in its switch) — nothing here needs the
// per-frame decode loop or the partial-push path those two require.
const int NOTE_PAGE = 5;
// Note buffer. The pane can draw at most 24 cols x 19 rows = 456 glyphs at
// text size 1, plus up to 19 newlines -> 475 bytes; 512 rounds that up and
// leaves room for the NUL. Anything larger would be RAM spent on characters
// that can never reach the screen at any of the three sizes. The server caps
// its own copy at 480 chars (NOTE_MAX_CHARS), so the snprintf that fills this
// is a belt-and-braces path that shouldn't fire against a well-behaved server.
// Named NOTE_BUF_MAX, not NOTE_MAX: esp32-hal-ledc.h already has a note_t
// enumerator called NOTE_MAX -- the musical kind -- and the two collide.
const int NOTE_BUF_MAX = 512;

// Bangkok has no DST, so a fixed UTC+7 offset is exact year-round.
const long GMT_OFFSET_SEC = 7 * 3600;
const int DST_OFFSET_SEC = 0;

const uint16_t COL_BG = 0x0841;      // near-black
const uint16_t COL_SURFACE = 0x0841; // card fill = bg (no surface tint)
const uint16_t COL_BORDER = 0x39C7;
const uint16_t COL_TEXT = 0xFFFF;
const uint16_t COL_TEXT2 = 0x9CD3;
const uint16_t COL_ACCENT = 0xFB08;  // orange
const uint16_t COL_GOOD = 0x2668;    // green
const uint16_t COL_SHINE_LO = 0x5ECE;  // COL_GOOD lerped ~25% to white (shine band edge)
const uint16_t COL_SHINE_MID = 0x9734; // ~50% to white (shine band mid)
const uint16_t COL_SHINE_HI = 0xD7BA;  // ~80% to white (shine band center)
const uint16_t COL_WARN = 0xF8C6;    // rose
const uint16_t COL_TRACK = 0x5ACB;   // neutral grey bar track
const uint16_t COL_TRACK_BLACK = 0x0000; // pure-black bar track (reset-countdown bars)
const uint16_t COL_BLUE = 0x3C1E;    // device-stats bar chart accent
const uint16_t COL_YELLOW = 0xFFE0;  // yellow for sun/lightning icons
// AQI badge colors (status page, see aqiColors() in pages.cpp) not otherwise
// used elsewhere -- Good/Moderate/Unhealthy reuse COL_GOOD/COL_YELLOW/COL_WARN.
const uint16_t COL_AQI_ORANGE = 0xFB82; // rgb(249,115,22) — Unhealthy for Sensitive Groups
const uint16_t COL_PURPLE = 0xAABE;     // rgb(168,85,247) — Very Unhealthy
const uint16_t COL_MAROON = 0x78E3;     // rgb(127,29,29) — Hazardous
// ── LAYOUT (480x320 landscape) ──────────────────────────────
// The CYD's 320x240 grid scaled ~1.5x across / ~1.33x down, then tuned per
// page. Shared by pages.cpp, gif_player.cpp, settings.cpp and the touch
// router in main.cpp; simulator-s3.html carries the same numbers.
//   - 3px outer margins, 2px gaps between cards (the CYD's 2px/2px rhythm)
//   - left column x 3..238, right column x 241..476, content y 3..289
//   - footer band y 292..319, 1px poll-progress line on y=319
const int LEFT_X = 3, LEFT_W = 236;
const int RIGHT_X = 241, RIGHT_W = 236;
const int CONTENT_Y1 = 290;   // exclusive bottom of the page content area
const int FOOTER_Y0 = 292;
// Tap split for page navigation: left half = previous page, right = next.
const int SWIPE_SPLIT_X = SCREEN_W / 2;

// Weather card hit-box on the status page (page 0): the whole card drawn at
// (RIGHT_X, 221, RIGHT_W, 69) in drawStatusPage. Tap opens the Weather overlay.
const int WEATHER_HIT_X0 = RIGHT_X, WEATHER_HIT_X1 = RIGHT_X + RIGHT_W;
const int WEATHER_HIT_Y0 = 221, WEATHER_HIT_Y1 = CONTENT_Y1;

// Footer CPU/ROM/RAM stats hit-box (drawFooter()'s "CPU x%  ROM x%  RAM x%"
// line). Tap opens the Device Stats overlay -- Device Stats is not one of the
// swiped PAGE_COUNT pages.
const int DEVICE_HIT_X0 = 56, DEVICE_HIT_X1 = 330;
const int DEVICE_HIT_Y0 = FOOTER_Y0, DEVICE_HIT_Y1 = SCREEN_H;

// Footer settings gear icon (drawSettingsIcon() in pages.cpp), bottom-right
// corner. Tap opens the Settings list (SET_LIST).
const int SETTINGS_HIT_X0 = 424, SETTINGS_HIT_X1 = SCREEN_W;
const int SETTINGS_HIT_Y0 = FOOTER_Y0, SETTINGS_HIT_Y1 = SCREEN_H;

// Cat pages only, and only while Cat Shuffle is FIXED: tapping advances to
// the next random cat instead of navigating. Two separate bands, because the
// cat only fills the whole screen on one of the two pages:
//   - GIF_PAGE (full-screen cat): the middle third of the screen. The outer
//     thirds deliberately fall through to the normal left/right page swipe,
//     which still splits at SWIPE_SPLIT_X (240) -- 160 and 320 sit either
//     side of it, so tapping outside this band navigates exactly as it does
//     on every other page. The CYD's first version of this claimed the whole
//     right half, which swallowed every forward tap and made later pages
//     unreachable from the cat pages (and from everywhere while offline,
//     since catMode is true on any page then). Don't widen this band back
//     over the split.
const int CAT_ADVANCE_X0 = 160, CAT_ADVANCE_X1 = 320;

// Screen-sleep pill (drawSleepButton() in pages.cpp), top-right corner of
// every screen -- pages, overlays, cats, settings. Anything else that wants
// that corner sits to its left (Battery Save icon below, the Weather AQI
// badge, the Settings list title). The hit box is deliberately bigger than
// the pill; tapping it is checked before every other touch target.
const int SLEEP_BTN_X0 = 432, SLEEP_BTN_Y0 = 8, SLEEP_BTN_W = 40, SLEEP_BTN_H = 9;  // centred on the battery icon's row
const int SLEEP_HIT_X0 = 416, SLEEP_HIT_X1 = SCREEN_W;
const int SLEEP_HIT_Y0 = 0, SLEEP_HIT_Y1 = 36;

// Battery Save top-right overlay, just left of the sleep pill:
// drawBatterySaveIcon()'s backing box (pages.cpp), x X0..X1 inclusive. Page
// content that must never be covered by it or the pill (the note pane's first
// text row) starts below both.
const int BATTERY_ICON_X0 = 400, BATTERY_ICON_X1 = 428;
const int BATTERY_ICON_Y0 = 3, BATTERY_ICON_Y1 = 21;

// Weather forecast slots delivered by /api/usage (Mac-proxied Open-Meteo)
// and cached on SD as /weather.json. Fixed-size arrays — no String/heap
// churn on every poll.
const int WEATHER_HOURLY_N = 6;
const int WEATHER_DAILY_N = 5;
struct WeatherHour {
  int8_t hour = -1;   // 0-23 local; -1 = empty slot
  int8_t tempC = 0;
  int16_t code = -1;  // WMO weather_code
};
struct WeatherDay {
  int8_t wday = -1;   // 0=Sun .. 6=Sat (tm_wday); -1 = empty
  int8_t high = 0;
  int8_t low = 0;
  int16_t code = -1;
};


// ── STATE ──────────────────────────────────────────────────
// projectNames/sessionResets/weekResets/weekModelName/weekModelResets are
// fixed char buffers rather than String: these fields get reassigned every
// ~20s poll for weeks of uptime on a no-PSRAM board, and repeated String
// reassignment (realloc when the new value's length differs from the old
// capacity) is exactly the kind of long-run heap churn that eventually
// fragments a small heap. Local, short-lived String concatenation elsewhere
// in the draw code (building one line of text, then discarding it before the
// next statement) doesn't have this problem and is left alone.
struct UsageState {
  char projectNames[5][32];
  int64_t projectTokens[5];
  int projectCount = 0;
  int64_t trend[7];
  int sessionPercent = -1;   // -1 = limits unavailable
  char sessionResets[24] = "";
  long sessionResetsInSec = -1;  // countdown to session reset; -1 = unknown
  int weekPercent = -1;
  char weekResets[24] = "";
  long weekResetsInSec = -1;     // countdown to week reset; -1 = unknown
  int64_t ctxTokens = -1;    // context window of the latest session; -1 = unknown -- int64_t
                             // for consistency with the other token fields (see their note above)
  int ctxPercent = -1;
  int weekModelPercent = -1; // per-model weekly limit; -1 = absent (row hidden)
  char weekModelName[24] = "";      // e.g. "Fable"
  char weekModelResets[24] = "";
  float creditsUsed = -1;    // extra-usage dollars; -1 = unavailable
  float creditsLimit = -1;
  int creditsPercent = -1;
  uint32_t lastFetchOkMs = 0;
  double btcPrice = -1;      // BTC/USDT (from the Mac via /api/usage); -1 = unknown
  int aqi = -1;              // Bangkok AQI, aqicn.org (from the Mac via /api/usage); -1 = unknown
  float weatherTempC = -999; // Bangkok temp (from the Mac via /api/usage); -999 = unknown
  int weatherCode = -1;      // WMO weather_code (from the Mac); -1 = unknown
  // Weather page fields — same payload as the status card's temp/code, plus
  // today's H/L, a short condition label, next 6h, and next 5d.
  // -999 / -1 / empty mean "not received yet" (show "--" / hide row).
  int weatherHigh = -999;
  int weatherLow = -999;
  char weatherCondition[20] = "";
  WeatherHour weatherHourly[WEATHER_HOURLY_N];
  uint8_t weatherHourlyCount = 0;
  WeatherDay weatherDaily[WEATHER_DAILY_N];
  uint8_t weatherDailyCount = 0;
  // Note page text + the board text size (1-3) chosen in note.html. Fixed
  // buffer for the same heap-churn reason as the other char fields above.
  char note[NOTE_BUF_MAX] = "";
  int noteSize = 1;
  bool sdOk = false;
  // Written by networkTask() (core 0) without stateMutex (see state.h's lock
  // comment: only the brief result-copy takes the lock, not this flag), read
  // by loop()/presentFrame()/GIFDraw()/openCatAtIndex() on core 1 -- volatile
  // for the same cross-core-visibility reason POLL_INTERVAL_MS is, since nothing
  // here serializes it.
  volatile bool haveData = false;    // true once any data (live or SD-cached) has been applied
};
// Defined in main.cpp.
extern UsageState STATE;

// Page/navigation state — owned by loop()'s touch handler (main.cpp),
// read by pages.cpp/settings.cpp/gif_player.cpp to know what's on screen.
extern int currentPage;
extern int cfgBootPage;
// Last page currentPage was on before the most recent restart -- kept up to
// date on every swipe regardless of cfgBootPage's mode, so switching Boot
// Page to AUTO always has a fresh value ready. Only actually used at boot
// when cfgBootPage == BOOT_PAGE_AUTO.
extern int cfgLastPage;
// Weather detail overlay (opened by tapping the weather card on page 0).
// Not a swipe-cycle page — same pattern as settingsScreen: any tap exits.
extern bool weatherPageOpen;
// Device Stats overlay (opened by tapping the footer's CPU/ROM/RAM stats
// line -- DEVICE_HIT_*). Same not-a-swipe-page pattern as weatherPageOpen.
extern bool devicePageOpen;
enum SettingsScreen { SET_OFF, SET_LIST, SET_LEAF };
extern SettingsScreen settingsScreen;
extern int settingsScrollOffset;  // vertical scroll position (px) of the SET_LIST list
extern int settingsLeafIndex;
const uint32_t CONFIRM_ARM_MS = 4000;
extern uint32_t confirmArmedMs;
extern int confirmArmedRow;
// Written by networkTask() (core 0), read by drawFooter()/loop()'s progress
// line (core 1) with no lock -- volatile for the same reason as
// STATE.haveData above.
extern volatile uint32_t lastPollMs;
extern uint32_t lastTouchMs;
// Screen sleep (main.cpp enterScreenSleep/exitScreenSleep): backlight 0,
// panel off, CPU 80MHz, WiFi modem sleep, polls floored like Battery Save.
// Read by applyEffectivePoll() on networkTask's core -- volatile.
extern volatile bool screenSleeping;

// ── PIXEL SHIFT (anti image-retention) ─────────────────────
const int8_t SHIFT_ORBIT[8][2] = {
  {0, 0}, {1, 0}, {2, 0}, {2, -1}, {2, -2}, {1, -2}, {0, -2}, {0, -1}
};
extern uint32_t cfgShiftStepMs;  // dwell per step; flash "pixel_shift_min", 0 disables
extern uint8_t shiftIdx;
extern int shiftX, shiftY;
extern uint32_t lastShiftMs;
extern bool shiftDirty;

// CPU load estimate + touch edge tracking.
extern float cpuPercentAvg;
extern bool touchWasDown;
// Did the most recent fetch reach the server? Written by networkTask() (core
// 0), read by drawFooter()/loop()'s progress line (core 1) with no lock --
// volatile for the same cross-core-visibility reason as STATE.haveData.
extern bool wifiOk;
extern volatile bool connected;

// Guards every read/write of the shared UsageState between the render loop
// (core 1) and the network task (core 0). Non-recursive —
// draw helpers must never re-lock. See main.cpp's networkTask()/
// pages.cpp's render() for the two sides of this.
extern SemaphoreHandle_t stateMutex;
inline void lockState()   { if (stateMutex) xSemaphoreTake(stateMutex, portMAX_DELAY); }
inline void unlockState() { if (stateMutex) xSemaphoreGive(stateMutex); }

// Serializes the SD_MMC card between the two cores (networkTask on core 0,
// and the page-5 GIF player reading frames from SD on the render core, core
// 1). NON-RECURSIVE and NON-NESTABLE with stateMutex in the reverse order:
// the only nesting allowed is sdMutex -> stateMutex.
extern SemaphoreHandle_t sdMutex;
inline void lockSD()   { if (sdMutex) xSemaphoreTake(sdMutex, portMAX_DELAY); }
inline void unlockSD() { if (sdMutex) xSemaphoreGive(sdMutex); }
// Bounded variant for the GIF player's per-frame decode, which is the ONLY
// high-frequency SD consumer (core 1, ~12fps). With an unbounded wait there,
// going offline actively sabotaged coming back: the cat screen is what plays
// during an outage, so core 0's recovery path (diag appends, cache reads,
// capacity refresh) queued behind a continuous stream of frame decodes.
// A skipped cat frame is invisible; a stalled poll is not. Returns false if
// the card was busy — callers must NOT unlock in that case.
inline bool tryLockSD(uint32_t waitMs) {
  if (!sdMutex) return true;
  return xSemaphoreTake(sdMutex, pdMS_TO_TICKS(waitMs)) == pdTRUE;
}

// Cat-shuffle interval: how long each cat GIF plays before rotating to a new
// random one. Defined in gif_player.cpp (gifTick reads it); settings.cpp's
// Cat Shuffle setting reads/writes it via getCurrentCatShuffle/applyCatShuffle.
extern uint32_t catShuffleMs;
// FIXED preset (Cat Shuffle value -1): disables all auto-rotation, so
// gifTick() just keeps replaying the current cat. main.cpp's touch
// handler reads this to decide whether a tap on the CATS/mixed page should
// manually advance to a new random cat.
extern bool catShuffleFixed;

// Runtime overrides for the compiled config.h defaults, loaded from internal
// flash (NVS, see sd_store.cpp's loadRuntimeConfig). Defined in main.cpp.
extern String cfgWifiSsid;
extern String cfgWifiPassword;
extern String cfgServerHost;
extern int cfgServerPort;
extern int cfgBrightness;
extern bool cfgNightModeOn;
extern bool nightDimActive;
const uint8_t NIGHT_MODE_DIM_VALUE = 64;  // ~25%, matches the brightness preset
// Battery Save: floor usage-poll interval only (no backlight change). User's
// Poll Interval preference stays stored; applyEffectivePoll() applies the floor.
// Mode is Settings OFF/ON/AUTO (persisted as flash key "battery_save"):
//   0 OFF  — never floor
//   1 ON   — always floor
//   2 AUTO — follow Mac /api/usage power.battery_save (default)
// serverBatterySave is the last *live* Mac flag (not applied from SD cache).
const int BATTERY_SAVE_OFF = 0;
const int BATTERY_SAVE_ON = 1;
const int BATTERY_SAVE_AUTO = 2;
// Written from the Settings leaf on core 1 (loop()), read from networkTask's
// applyEffectivePoll()/batterySaveActive() call chain on core 0 -- volatile
// like cfgPollIntervalSec above.
extern volatile int cfgBatterySaveMode;
extern volatile bool serverBatterySave;
const uint32_t BATTERY_SAVE_POLL_SEC = 120;   // 2 min minimum while mode is on
inline bool batterySaveActive() {
  if (cfgBatterySaveMode == BATTERY_SAVE_ON) return true;
  if (cfgBatterySaveMode == BATTERY_SAVE_AUTO) return serverBatterySave;
  return false;
}
// Green reset-countdown bars (under 5h/week) + analog-clock timer wedge.
// Green reset hand on the clock is always drawn when a reset is known.
extern bool cfgShowCountdown;
// Status-page AQI badge next to the date (see aqiColors()/drawStatusPage in
// pages.cpp). Default on; toggled from the Settings area like cfgShowCountdown.
extern bool cfgShowAqi;
// On-the-hour signal: 6s of 1Hz display inversion at :00 (see
// checkHourlyFlash(), which returns false outright when this is off, so every
// consumer -- presentFrame's invertDisplay, the poll-progress line and the
// shine sweep's skip-while-inverted guards -- goes quiet together).
extern bool cfgHourlyFlash;
// The 1px poll-countdown line along the bottom edge (drawFooter's tail plus
// loop()'s between-render top-up). Off = no line at all.
extern bool cfgShowProgress;
extern int cfgScreenRotation;

// Generic Settings-page persistence queue: a leaf's apply() (loop(), core 1)
// mutates its live global directly, then queues the flash key/value here;
// networkTask (core 0) drains it so the flash write never happens on the
// render core. Defined in main.cpp (networkTask drains it);
// queued from settings.cpp.
extern volatile bool pendingConfigSave;
extern volatile uint8_t pendingConfigKeyId;
extern volatile int32_t pendingConfigValue;
extern volatile bool pendingForgetWifi;
// Restart (Settings' RESTART row) is armed here rather than calling
// ESP.restart() directly from core 1 -- that could cut power to core 0
// mid-SD-write (under sdMutex). networkTask drains this between its own
// sequential SD operations, so the restart only ever happens once nothing
// is in flight.
extern volatile bool pendingRestart;

// Config keys persisted through the generic queue above (see
// sd_store.cpp's saveIntConfigToFlash and settings.cpp's queueConfigSave).
enum ConfigKeyId {
  CFGKEY_BRIGHTNESS = 0, CFGKEY_POLL_INTERVAL, CFGKEY_PIXEL_SHIFT, CFGKEY_BOOT_PAGE,
  CFGKEY_CAT_SHUFFLE, CFGKEY_NIGHT_MODE, CFGKEY_ROTATION, CFGKEY_SHOW_COUNTDOWN,
  CFGKEY_BATTERY_SAVE, CFGKEY_SHOW_AQI, CFGKEY_HOURLY_FLASH, CFGKEY_SHOW_PROGRESS,
  CFGKEY_LAST_PAGE,
  CFGKEY_COUNT
};
extern const char* const CONFIG_KEY_NAMES[CFGKEY_COUNT];

// ── FORMATTING (format.cpp) ────────────────────────────────
String fmtTokens(int64_t t);
String fmtCost(float c);
String fmtBtc(double p);
String fmtCountdown(long sec);
String fmtCountdownDHM(long sec);
String fmtKB(uint32_t bytes);
String fmtGB(uint64_t bytes);
int flashPercent(uint32_t &usedOut, uint32_t &totalOut);
// Internal-SRAM heap in use as a % of its total (heap_caps MALLOC_CAP_INTERNAL),
// and the same for the 8MB PSRAM -- the S3's two real memory pools, replacing
// the CYD's single fixed DRAM constant.
int staticRamPercent(uint32_t &usedOut, uint32_t &totalOut);
int psramPercent(uint32_t &usedOut, uint32_t &totalOut);
// SD_MMC.totalBytes()/usedBytes() touch the card that sdMutex serializes
// between networkTask (core 0) and the render core's cat-GIF reads (core 1)
// -- see sdMutex's contract above. refreshSdCapacityCache() does the live
// query under lockSD()/unlockSD() and must only be called from networkTask;
// drawDevicePage() (core 1, called from inside render()'s stateMutex-held
// section, so it cannot also take sdMutex -- only sdMutex -> stateMutex
// nesting is allowed, never the reverse) reads the cached result instead via
// cachedSdCapacityPercent(), no lock needed.
void refreshSdCapacityCache();
int cachedSdCapacityPercent(uint64_t &usedOut, uint64_t &totalOut);

// ── NETWORK (net.cpp) ──────────────────────────────────────
void ensureMdns();
void connectWifi();
bool resolveServer();
bool fetchUsage();
bool loadCachedUsage();
void loadEnvCache();
void loadWeatherCache();
// fromNetwork=true only for a live /api/usage fetch — applies power.battery_save
// into serverBatterySave. SD cache loads pass false so a stale Mac power flag
// can't floor the poll across an overnight offline boot.
bool applyUsageJson(const String& payload, bool fromNetwork = false);
// Set false by networkTask() (main.cpp) on WiFi loss, so ensureMdns()
// re-initializes once WiFi returns.
extern bool mdnsStarted;

// ── SD STORE (sd_store.cpp) ────────────────────────────────
// Runtime settings/config now persist to internal flash (NVS, via
// Preferences) rather than the SD card -- see loadRuntimeConfig()'s comment
// in sd_store.cpp. SD is still used here for diagnostics and the boot splash.
void loadRuntimeConfig();
const char* resetReasonStr();
void logDiag(const char* event);
bool drawBmpFromSD(const char* path, int dx, int dy);
void showBootSplash();
void saveWifiCredsToFlash(const String& ssid, const String& password);
void saveIntConfigToFlash(const char* key, int32_t value);
// Cached last-known server IPv4 (raw 4 bytes in an int32). Not a Settings-area
// key — see the comment on loadServerIpFromFlash() in sd_store.cpp.
const char* const SERVER_IP_KEY = "server_ip";
uint32_t loadServerIpFromFlash();

// Floor on how often the poll loop touches the SD card for the JSON caches,
// /archive.csv and the capacity stats. Decoupled from Poll Interval so the 5s
// setting doesn't turn every poll into five file operations — see the comments
// at fetchUsage()'s persist block (net.cpp) and networkTask() (the .ino).
const uint32_t SD_PERSIST_MIN_MS = 60000UL;
void forgetWifiFromFlash();

// ── PAGES / RENDER (pages.cpp) ─────────────────────────────
void render();
void drawMixedPageStatic();
// Mixed page's cat pane (right column): the GIF player cover-fits the cat
// into it. Defined here so pages.cpp's placeholder and gif_player.cpp agree.
const int MIXED_GIF_X0 = 240, MIXED_GIF_W = 240;
const int MIXED_GIF_Y0 = 3, MIXED_GIF_H = CONTENT_Y1 - 3;
//   - MIXED_PAGE (cat pane on the right half only): the left half of the
//     pane, bounded to the pane's own height so it doesn't reach down into
//     the footer below it. MIXED_GIF_X0 already sits at SWIPE_SPLIT_X, so the
//     pane's right half is untouched and keeps doing the normal forward
//     swipe -- only the pane's left half changes from "next page" to "next
//     cat".
const int MIXED_CAT_ADVANCE_X0 = MIXED_GIF_X0, MIXED_CAT_ADVANCE_X1 = MIXED_GIF_X0 + MIXED_GIF_W / 2;
const int MIXED_CAT_ADVANCE_Y0 = 0, MIXED_CAT_ADVANCE_Y1 = CONTENT_Y1;
void drawBatterySaveIcon();
void drawSleepButton();

// ── GIF PLAYER (gif_player.cpp) ─────────────────────────────
void scanCats();
// Returns true when it changed `frame` -- loop() presents once per pass.
bool gifTick(bool offline);
// Encapsulate what used to be loop() reaching directly into gif/gifOpen/
// gifNextFrameMs — narrows the extern surface to three intent-named calls
// instead of four raw globals.
void gifPlayerEnterCatMode();       // allocate the decoder on entering a cat page (or offline)
void gifPlayerExitCatMode();        // free the decoder on leaving
void gifPlayerResetForPageChange(); // force a fresh random GIF on the next tick
// Decode + draw the first frame right now (into `frame`, then presentFrame()
// -- held during a page slide), instead of waiting for the next gifTick().
void gifPlayerPrimeFrame(bool offline);

// ── SETTINGS (settings.cpp) ────────────────────────────────
void renderSettings();
void queueConfigSave(uint8_t keyId, int32_t value);
// Recompute live backlight (night mode) / poll cadence (battery save). Call
// after any of those inputs change (and once after loadRuntimeConfig).
void applyEffectiveBrightness();
void applyEffectivePoll();
// Handles a tap while settingsScreen == SET_LEAF; returns true if it consumed
// the tap. Keeps the SettingDef/button-layout internals out of
// main.cpp entirely.
bool handleSettingsTouch(int32_t tx, int32_t ty, uint32_t now, bool catMode);
// SET_LIST is drag-to-scroll rather than tap-driven, so it needs the full
// down/move/up gesture instead of one tap callback: Begin records the touch
// start, Move live-updates settingsScrollOffset while the finger is down, End
// decides whether the gesture was a tap (opens a leaf / exits) or a scroll
// (total movement over DRAG_TAP_PX in settings.cpp) and does nothing further.
void settingsListDragBegin(int32_t tx, int32_t ty);
void settingsListDragMove(int32_t tx, int32_t ty);
void settingsListDragEnd(bool catMode);

// ── AP SETUP (ap_setup.cpp) ────────────────────────────────
void runApSetup();  // blocks until configured, then ESP.restart()s — never returns
