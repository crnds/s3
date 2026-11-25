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
#include "audio.h"
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
#include "format_pure.h"
#include "pace.h"
#include "timing.h"
#include "layout.h"
#include "config_keys.h"
#include "ota.h"
#include "net.h"
#include "format.h"
#include "sd_store.h"
#include "pages.h"
#include "gif_player.h"
#include "movie.h"
#include "settings.h"
#include "nav.h"
#include "ap_setup.h"

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
// pace sweep per new value (design.md 12.7).
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
// Pages (0-indexed): 0 Status, 1 Usage (limits + projects + trend), 2 Cats,
// 3 Movies, 4 Status + cats, 5 Note, 6 Overview.
const int GIF_PAGE = 2;    // 3rd page: random cat GIFs, embedded in the firmware (cats_embed.h)
const int MOVIE_PAGE = 3;  // 4th page: random movies from /movies/ on SD (see movie_player.cpp)
const int MIXED_PAGE = 4;  // 5th page: status + cats split
// 6th page: the same left column as MIXED_PAGE, but the right half shows the
// note text from note.html instead of cats. Unlike GIF_PAGE/MOVIE_PAGE/
// MIXED_PAGE this is an ordinary render() page (a `case` in its switch) —
// nothing here needs the per-frame decode loop or the partial-push path
// those three require.
const int NOTE_PAGE = 5;
// 7th page: the cards in miniature on a 3x3 grid (design.md 7.5), one cell
// of which plays the cat -- so, like MIXED_PAGE, a media page with a static
// half that gif_player.cpp plays around (catPanePage()).
const int OVERVIEW_PAGE = 6;
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

const int SLEEP_HIT_Y0 = 0, SLEEP_HIT_Y1 = TOK_CORNER_HIT_Y1;

// Weather forecast slots delivered by /api/usage (Mac-proxied Open-Meteo)
// and cached on SD as /weather.json. Fixed-size arrays — no String/heap
// churn on every poll.
const int WEATHER_HOURLY_N = 11;
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
// ~20s poll for weeks of uptime on a small internal heap, and repeated String
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
  // The server's active_now (Claude Code wrote something in the last 180 s),
  // from live polls only; -1 = none yet. Drives the Claude ding (main.cpp).
  int8_t claudeActive = -1;
  double btcPrice = -1;     // BTC/USDT (from the Mac via /api/usage); -1 = unknown
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
extern uint32_t confirmArmedMs;
extern int confirmArmedRow;
// Written by networkTask() (core 0), read by drawFooter()/loop()'s progress
// line (core 1) with no lock -- volatile for the same reason as
// STATE.haveData above.
extern volatile uint32_t lastPollMs;
// millis() of the last successful *live* poll; 0 = none since boot (what is on
// screen, if anything, is the SD cache). Written by networkTask, read by
// dataStale() on the render side.
extern volatile uint32_t lastFetchSuccessMs;
extern uint32_t lastTouchMs;
// Screen sleep (main.cpp enterScreenSleep/exitScreenSleep): backlight 0,
// panel off, CPU 80MHz, WiFi modem sleep, polls floored like Battery Save.
// Read by applyEffectivePoll() on networkTask's core -- volatile.
extern volatile bool screenSleeping;

// ── PIXEL SHIFT (anti image-retention) ─────────────────────
// The orbit itself (SHIFT_ORBIT) is pages.cpp's.
extern uint32_t cfgShiftStepMs;  // dwell per step; flash "pixel_shift_min", 0 disables
extern uint8_t shiftIdx;
extern int shiftX, shiftY;
extern uint32_t lastShiftMs;
extern bool shiftDirty;

// CPU load estimate.
extern float cpuPercentAvg;
// Did the most recent fetch reach the server? Written by networkTask() (core
// 0), read by drawFooter()/loop()'s progress line (core 1) with no lock --
// volatile for the same cross-core-visibility reason as STATE.haveData.
extern bool wifiOk;
extern volatile long fixtureEpoch;   // the wall clock fixtureMode holds still
extern volatile bool fixtureMode;  // serial 'F': canned data, polling frozen (tools/parity.py)
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
// Bounded variant for the movie player's per-frame read, which is the ONLY
// high-frequency SD consumer (core 1, ~10fps; the cat GIFs are embedded in
// flash and never touch the card). With an unbounded wait there, going
// offline actively sabotaged coming back: the media pages play during an
// outage, so core 0's recovery path (diag appends, cache reads, capacity
// refresh) queued behind a continuous stream of frame reads.
// A skipped movie frame is invisible; a stalled poll is not. Returns false if
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
// "Hourly signal" (flash key hourly_flash): the panel inverts (INVON/INVOFF,
// displaySetInvert) on the even seconds of hh:00:00..05 -- three flashes.
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
// Dark / Light theme ("light_mode" NVS key): false = Dark. Applied to the
// runtime colour tokens by applyThemeTokens() (settings.cpp).
extern bool cfgLightMode;
// Speaker (audio.h), all under Settings > Sound. cfgTapVol ("tap_level") is the
// tap tock's volume, cfgAlertVol ("sound_level") the chime / Claude ding /
// alerts' -- each 0 (Off) to SOUND_VOL_MAX. The bools are per-category switches
// and the two mute rules, all default on ("hourly_chime", "claude_ding",
// "sound_alerts", "mute_sleep", "mute_night").
extern int cfgTapVol, cfgAlertVol;
extern bool cfgHourlyChime, cfgClaudeDing, cfgSoundAlerts;
extern bool cfgMuteSleep, cfgMuteNight;


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

