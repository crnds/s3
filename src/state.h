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
#include "tokens.h"
#include "motion.h"

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

// Push `frame` to the panel (pixel shift applied on the way). No-op while
// presentHold is set (a screen is being composed off-screen). While a
// transition is running (nav.cpp: page slide, sheet, push, cross-fade) it
// only marks the frame dirty -- navTick() composites and presents it once
// per loop pass at the spring's current offset.
void presentFrame();
extern bool presentHold;
// Running total of time presents spent idle waiting for the panel's TE edge
// (main.cpp subtracts it from the CPU duty-cycle estimate).
extern volatile uint32_t teWaitAccumUs;
bool pixelShiftTick(uint32_t now);
// Between-render top-ups (the per-poll pace sweep, the status-dot pulse and
// the poll-progress hairline). They draw straight into `frame`, return true
// when they changed pixels, and loop() then presents once for the pass.
bool shineTick(uint32_t nowMs);
bool progressTick(uint32_t nowMs);
bool pulseTick(uint32_t nowMs);
// Bumped by networkTask on every successful poll; the render side starts one
// pace sweep and one status-dot pulse per new value (design.md 12.7).
extern volatile uint32_t pollOkSeq;

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
const int PAGE_COUNT = 7;
// cfgBootPage sentinel: resume whichever page was on screen before the last
// restart (cfgLastPage), rather than a fixed page. See the Boot Page setting.
const int BOOT_PAGE_AUTO = -1;
const int GIF_PAGE = 3;    // 4th page (0-indexed): random cat GIFs from /cats/ on SD
const int MOVIE_PAGE = 4;  // 5th page: random movies from /movies/ on SD (see movie_player.cpp)
const int MIXED_PAGE = 5;  // 6th page: status + cats split
// 7th page: the same left column as MIXED_PAGE, but the right half shows the
// note text from note.html instead of cats. Unlike GIF_PAGE/MOVIE_PAGE/
// MIXED_PAGE this is an ordinary render() page (a `case` in its switch) —
// nothing here needs the per-frame decode loop or the partial-push path
// those three require.
const int NOTE_PAGE = 6;
// Note buffer. The server caps its copy at 480 chars (NOTE_MAX_CHARS); 512
// leaves room for the NUL. The S3 pane (37 cols x 14 rows at size 1) could
// show slightly more than that, but the server cap, not the pane, is the
// limit -- the snprintf that fills this is a belt-and-braces path that
// shouldn't fire against a well-behaved server.
// Named NOTE_BUF_MAX, not NOTE_MAX: esp32-hal-ledc.h already has a note_t
// enumerator called NOTE_MAX -- the musical kind -- and the two collide.
const int NOTE_BUF_MAX = 512;

// Bangkok has no DST, so a fixed UTC+7 offset is exact year-round.
const long GMT_OFFSET_SEC = 7 * 3600;
const int DST_OFFSET_SEC = 0;

// ── LAYOUT (480x320 landscape) ──────────────────────────────
// design.md section 7 is the source of truth; the numbers here are the
// shared hit boxes and card boxes the touch router (nav.cpp), pages.cpp,
// settings.cpp and gif_player.cpp all need. simulator-s3.html carries the
// same numbers. Colours, type, spacing and component sizes are tokens
// (tokens.h) -- no COL_* literals any more.
//   - 8px screen margin and card gutter; content x 8..471, y 8..279
//   - left column x 8..179 (fixed reservation), right column x 188..471
//   - status strip y 288..318, progress hairline y 319
// Page navigation splits at the screen half, independent of the grid.
const int SWIPE_SPLIT_X = TOK_LAYOUT_HALF_SPLIT_X;

// Status page cards (design.md 7.4).
const int LIMIT_CARD_H = 112, BTC_CARD_H = 32;
const int LIMIT5H_Y = TOK_LAYOUT_CONTENT_Y0;                              // 8
const int LIMITWK_Y = LIMIT5H_Y + LIMIT_CARD_H + TOK_SPACE_GUTTER;       // 128
const int BTC_Y = LIMITWK_Y + LIMIT_CARD_H + TOK_SPACE_GUTTER;           // 248
const int CLOCK_CARD_H = 192, WEATHER_CARD_H = 72;
const int CLOCK_CARD_Y = TOK_LAYOUT_CONTENT_Y0;                           // 8
const int WEATHER_CARD_Y = CLOCK_CARD_Y + CLOCK_CARD_H + TOK_SPACE_GUTTER;  // 208

// Weather card (status page) = its own hit box: the whole card is the
// target, with a disclosure chevron as its affordance. Tap opens the Weather
// sheet.
const int WEATHER_HIT_X0 = TOK_LAYOUT_COL_RIGHT_X, WEATHER_HIT_X1 = TOK_LAYOUT_CONTENT_X1;
const int WEATHER_HIT_Y0 = WEATHER_CARD_Y, WEATHER_HIT_Y1 = TOK_LAYOUT_CONTENT_Y1;

// Status strip targets (design.md 11.12): 56x40 each, reaching up into the
// 8px gap above the strip (touch.edge.min for a bottom-edge target).
const int STRIP_HIT_Y0 = TOK_LAYOUT_CONTENT_Y1;                           // 280
const int HEALTH_HIT_X0 = 0, HEALTH_HIT_X1 = 56;                          // -> Device Stats
const int SETTINGS_HIT_X0 = 424, SETTINGS_HIT_X1 = SCREEN_W;              // -> Settings
const int STRIP_CY = 303;                                                 // glyph centre line
const int STRIP_CAPTION_Y = 295;                                          // caption text top (design.md 11.12)

// Media control (design.md 11.20): a shuffle icon button, only while Cat
// Shuffle is Fixed, bottom-right of the media area, 8px in from its edges.
// It replaces the old invisible "next cat" zones.
const int SHUFFLE_DISC_R = 16;
const int SHUFFLE_HIT = 48;

// Sleep button (corner slot a) -- checked before every other target.
const int SLEEP_HIT_X0 = TOK_CORNER_HIT_X0, SLEEP_HIT_X1 = SCREEN_W;
const int SLEEP_HIT_Y0 = 0, SLEEP_HIT_Y1 = TOK_CORNER_HIT_Y1;

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
  double btcChangePct = NAN; // rolling 24h change in %; NAN = unknown
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
// Weather sheet (tap the status page's weather card). Not a carousel page:
// a read-only sheet -- close glyph, tap anywhere or drag down dismisses.
extern bool weatherPageOpen;
// Device Stats sheet (tap the status strip's health glyphs). Same pattern.
extern bool devicePageOpen;
enum SettingsScreen { SET_OFF, SET_LIST, SET_LEAF };
extern SettingsScreen settingsScreen;
extern int settingsScrollOffset;  // vertical scroll position (px) of the SET_LIST list (may overshoot while rubber-banding)
extern int settingsLeafIndex;
const uint32_t CONFIRM_ARM_MS = TOK_TOUCH_ARM_WINDOW_MS;
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
// Count of movies found in /movies/ at boot (movie_player.cpp's scanMovies).
// gif_player.cpp's drawMediaOverlays() reads this instead of catCount while
// on MOVIE_PAGE, to decide whether there's anything to shuffle to.
extern int movieCount;

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
// "Pace bars" setting (flash key show_countdown, kept for compatibility): the
// green pace meters under the 5h/week usage meters + the clock's pace wedge.
// The green reset hand on the clock is always drawn when a reset is known.
extern bool cfgShowCountdown;
// Status-page AQI badge next to the date (see aqiColors()/drawStatusPage in
// pages.cpp). Default on; toggled from the Settings area like cfgShowCountdown.
extern bool cfgShowAqi;
// "Hourly signal" (flash key hourly_flash): a backlight breath on the hour
// (motion.backlight.breath) -- it replaced the old 6s screen inversion.
extern bool cfgHourlyFlash;
// "Poll progress": the 1px poll-countdown hairline along y 319. Off = none.
extern bool cfgShowProgress;
extern int cfgScreenRotation;
// Accessibility (design.md 12.9). Reduce Motion swaps every spring slide and
// sheet for a 3-frame cross-fade and drops the lean, scrim steps, sweep and
// pulse. Increase Contrast brightens text.secondary / fill.track and outlines
// cards (applyContrast() in settings.cpp).
extern bool cfgReduceMotion;
extern bool cfgHighContrast;

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
  CFGKEY_LAST_PAGE, CFGKEY_REDUCE_MOTION, CFGKEY_HIGH_CONTRAST,
  CFGKEY_COUNT
};
extern const char* const CONFIG_KEY_NAMES[CFGKEY_COUNT];

// ── FORMATTING (format.cpp) ────────────────────────────────
String fmtTokens(int64_t t);
String fmtCost(float c);
String fmtBtc(double p);
String fmtChangePct(double pct);  // "+5%" / "-3%" (whole percent, sign always)
String fmtCountdown(long sec);     // "4h 03m" / "42m"
String fmtCountdownDHM(long sec);  // "6d 16h" / "4h 03m"
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
// render() composes the current screen (a page, or the Weather / Device
// Stats sheet) into `frame` and presents it. Settings has its own
// renderSettings().
void render();
void drawMixedPageStatic();
// Mixed page's cat pane = the right column (design.md 7.4): the GIF player
// cover-fits the cat into it. Defined here so pages.cpp's placeholder and
// gif_player.cpp agree.
const int MIXED_GIF_X0 = TOK_LAYOUT_COL_RIGHT_X, MIXED_GIF_W = TOK_LAYOUT_COL_RIGHT_W;
const int MIXED_GIF_Y0 = TOK_LAYOUT_CONTENT_Y0, MIXED_GIF_H = TOK_LAYOUT_CONTENT_Y1 - TOK_LAYOUT_CONTENT_Y0;
// System corner glyphs, drawn LAST on every screen (design.md 11.21).
// overMedia puts each occupied slot on a color.plate backing.
void drawSystemCorner(bool overMedia);
// Shared icon/component primitives settings.cpp and gif_player.cpp reuse.
void drawCloseGlyph(int cx, int cy, uint16_t c);
void drawBackGlyph(int cx, int cy, uint16_t c);
void drawChevron(int cx, int cy, uint16_t c);
void drawShuffleButton(int cx, int cy, bool pressed);
void drawIconButtonPressed(int cx, int cy);
void drawModalHeader(bool back, const char* title, bool pressed);
void drawCardSurface(int x, int y, int w, int h, uint16_t fill);
// Anti-aliased fills (design.md 8.1): LovyanGFX's fillSmoothRoundRect /
// fillSmoothCircle algorithm, blended straight into the frame buffer.
void aaFillRoundRect(int x, int y, int w, int h, int r, uint16_t c);
void aaFillCircle(int x, int y, int r, uint16_t c);
void aaRing(int cx, int cy, int r, int t, uint16_t c);  // AA ring, outer radius r, thickness t
// The full-screen cat page's reset readout, on a color.plate (design.md 11.20).
void drawResetPlate();
// Empty / error state (design.md 13.4), centred in a box: a title, space.sm,
// a caption description. isError puts the title in status.error.
void drawEmptyState(int cx, int y0, int h, const char* title, const char* desc, bool isError,
                    FontId titleFont = TOK_TYPE_HEADLINE, uint16_t titleColor = TOK_COLOR_TEXT_PRIMARY);
// The shuffle button's centre for the current media layout (full screen or
// the mixed pane).
void shuffleCentre(bool mixed, int& cx, int& cy);

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
// Re-blit the whole open GIF canvas (+ the page's static parts and
// overlays) into `frame` -- after a sheet or a cancelled lean overwrote it.
void gifPlayerRepaint(bool offline);
// Redraw just the overlays (reset plate, shuffle button, corner) and present
// -- a pressed state changed; each overlay keeps its footprint, so nothing
// under it needs restoring.
void gifPlayerRedrawOverlays(bool offline);
// Reset plate + shuffle button + corner glyphs, drawn over whichever media is
// active. Shared with movie_player.cpp: MOVIE_PAGE uses the same overlays.
void drawMediaOverlays(bool offline);

// ── MOVIE PLAYER (movie_player.cpp) ─────────────────────────
// Random .mjpeg playback on MOVIE_PAGE, the same shape as the GIF player
// above but for raw MJPEG streams from /movies/ on the SD card. Reuses
// catShuffleMs/catShuffleFixed (the "Cat Shuffle" setting) for its own
// rotation timing -- see movie_player.cpp for why. gif_player.cpp's gifTick()
// and its five sibling entry points each dispatch into this module's
// equivalents when currentPage == MOVIE_PAGE, so main.cpp/nav.cpp need no
// separate call sites for movies.
void scanMovies();
bool movieTick(bool offline);
void moviePlayerEnter();
void moviePlayerExit();
void moviePlayerResetForPageChange();
void moviePlayerPrimeFrame(bool offline);
void moviePlayerRepaint(bool offline);

// ── SETTINGS (settings.cpp) ────────────────────────────────
// The Settings sheet: SET_LIST (scrolling list of rows) and SET_LEAF (a
// detail screen with an option grid or an arm button). nav.cpp owns the
// gestures and transitions; these are the drawing, hit-testing and commit
// halves.
void renderSettings();          // compose + present
void drawSettingsScreen();      // compose only (transitions snapshot around it)
void queueConfigSave(uint8_t keyId, int32_t value);
// Recompute live backlight (night mode) / poll cadence (battery save). Call
// after any of those inputs change (and once after loadRuntimeConfig).
// fadeMs picks the backlight motion token (0 = cut).
void applyEffectiveBrightness(uint32_t fadeMs = 0);
void applyEffectivePoll();
void applyContrast();
int settingsListHit(int32_t x, int32_t y);        // row index, or -1
// Tap on a list row (commit on up): toggles flip in place and return false;
// navigation/action rows set settingsLeafIndex and return true (push it).
bool settingsListActivate(int idx);
int settingsLeafHit(int32_t x, int32_t y);        // option cell / arm button, or -1
void settingsLeafActivate(int idx, uint32_t now);
int settingsScrollMax();
// Toast (design.md 11.17): confirms an action with no other visible result.
extern char toastText[40];
extern uint32_t toastUntilMs;

// ── NAVIGATION (nav.cpp) ───────────────────────────────────
// The touch router, gesture recogniser and every transition (page slide,
// lean, swipe, sheet rise/drop with scrim, Settings push/pop, reduced-motion
// cross-fade). design.md sections 9 and 12.
enum PressId {
  PRESS_NONE = 0, PRESS_SLEEP, PRESS_CLOSE, PRESS_WEATHER, PRESS_HEALTH, PRESS_GEAR,
  PRESS_SHUFFLE, PRESS_ROW, PRESS_CELL
};
extern PressId pressedId;
extern int pressedIndex;          // row / cell index for PRESS_ROW / PRESS_CELL
void navTouch(bool down, int32_t x, int32_t y, uint32_t now);
// Steps the running spring / momentum and presents. Returns true if it
// presented this pass (loop() then skips its own present).
bool navTick(uint32_t now);
bool navTransitionActive();       // a composite (slide / sheet / fade) is on screen
bool navSheetOpen();              // Weather, Device Stats or Settings is up
void navGoToPage(int page, bool forward);   // serial keys; animated unless offline
void navOpenSheet(int which);     // 0 weather, 1 device, 2 settings
void navCloseSheet();
void navFinishTransition();       // snap any running transition to its end
bool navCatLayout();              // the cat player owns the current page
void navSyncCatMode(bool catMode);  // allocate / free the GIF decoder on catMode edges
void navResetGesture();           // drop any gesture in progress (screen sleep)
// Screen sleep (main.cpp): backlight fade to 0, panel DISPOFF + SLPIN, CPU
// 80MHz, WiFi modem sleep; the next press anywhere wakes (and is swallowed).
void enterScreenSleep();
void exitScreenSleep();
// Debug time scale (serial 'm', simulator ?slowmo=10): multiplies every
// motion token's time (design.md 17).
extern float motionTimeScale;

// ── AP SETUP (ap_setup.cpp) ────────────────────────────────
void runApSetup();  // blocks until configured, then ESP.restart()s — never returns
