// Claude Code token usage dashboard for the JC3248W535EN (ESP32-S3, 3.5"
// 480x320). A port of ~/cyd's CYD firmware: same Mac server contract
// (/api/usage), same pages, settings, offline/cat behaviour and two-core
// split -- re-laid out for the bigger panel, with anti-aliased fonts, a
// TE-synced full-frame present, native-rate cat GIFs and page slides.
//
// This file holds the frame/state object definitions, setup(), loop(), the
// touch router and networkTask(); everything else is split into format.cpp,
// net.cpp, sd_store.cpp, pages.cpp, gif_player.cpp, settings.cpp,
// ap_setup.cpp, display.cpp, touch_axs.cpp and fonts.cpp, all declared through
// state.h.
#include "state.h"

// ── FRAME ──────────────────────────────────────────────────
// Off-screen frame (480x320 RGB565, 300KB in PSRAM): every page is composed
// here and handed to the panel in one TE-synced present, so redraws never
// flash. There is no panel-direct fallback -- the AXS15231B only takes whole
// frames -- so a failed allocation at boot is fatal (logged, then restart).
LGFX_Sprite frame;
lgfx::LovyanGFX* g = &frame;

// ── STATE ──────────────────────────────────────────────────
// Non-const: overridable from flash (see sd_store.cpp's loadRuntimeConfig).
// cfgPollIntervalSec is the user's preference; POLL_INTERVAL_MS is the
// effective cadence networkTask reads every cycle (Battery Save may stretch it).
volatile uint32_t cfgPollIntervalSec = 20;
volatile uint32_t POLL_INTERVAL_MS = 20000;

UsageState STATE;

int currentPage = 0;
int cfgBootPage = 0;  // which page currentPage starts on; overridable via flash "boot_page"
int cfgLastPage = 0;  // last page shown before the most recent restart; flash "last_page"
bool weatherPageOpen = false;  // Weather sheet (tap the status page's weather card)
bool devicePageOpen = false;   // Device Stats sheet (tap the status strip's health glyphs)
SettingsScreen settingsScreen = SET_OFF;
int settingsScrollOffset = 0;  // vertical scroll position (px) of the SET_LIST list
int settingsLeafIndex = -1;    // index into SETTINGS[] currently open in SET_LEAF
uint32_t confirmArmedMs = 0;
int confirmArmedRow = -1;
volatile uint32_t lastPollMs = 0;
uint32_t lastTouchMs = 0;
volatile bool screenSleeping = false;

// ── PIXEL SHIFT (anti image-retention) ─────────────────────
// Applied at present time by display.cpp, so a step needs only a re-present,
// never a redraw.
uint32_t cfgShiftStepMs = 180000;  // dwell per step; flash "pixel_shift_min", 0 disables
uint8_t shiftIdx = 0;
int shiftX = 0, shiftY = 0;
uint32_t lastShiftMs = 0;
bool shiftDirty = false;

// CPU load estimate: no FreeRTOS runtime stats in the Arduino build, so this
// is the render loop's duty cycle -- work time per pass vs the pass period,
// minus time spent idle waiting on the panel's TE edge -- smoothed with an EMA.
float cpuPercentAvg = 0;
bool touchWasDown = false;
bool wifiOk = false;
volatile bool connected = false;  // did the most recent fetch reach the server?

// Guards every read/write of the shared UsageState between the render loop
// (core 1) and the network task (core 0). See state.h for the locking contract.
SemaphoreHandle_t stateMutex = nullptr;
SemaphoreHandle_t sdMutex = nullptr;

// Runtime overrides for the compiled config.h defaults, loaded from internal
// flash (NVS, see sd_store.cpp's loadRuntimeConfig()).
String cfgWifiSsid = WIFI_SSID;
String cfgWifiPassword = WIFI_PASSWORD;
String cfgServerHost = SERVER_HOST;
int cfgServerPort = SERVER_PORT;
int cfgBrightness = 200;   // 0-255 panel backlight; overridable via flash
// Night mode: fixed 23:00-07:00 schedule (Bangkok has no DST, so tm_hour is
// already local), dims to a fixed 25% while on. Checked once/sec in loop().
bool cfgNightModeOn = false;
// flash "battery_save": 0=OFF, 1=ON, 2=AUTO (follow Mac). serverBatterySave is
// the last live power.battery_save.
volatile int cfgBatterySaveMode = BATTERY_SAVE_AUTO;
volatile bool serverBatterySave = false;
bool cfgShowCountdown = true;  // default on; flash "show_countdown"
bool cfgShowAqi = true;        // default on; flash "show_aqi"
bool cfgHourlyFlash = true;    // default on; flash "hourly_flash"
bool cfgShowProgress = true;   // default on; flash "show_progress"
bool cfgReduceMotion = false;  // default off; flash "reduce_motion"
bool cfgHighContrast = false;  // default off; flash "high_contrast"
bool nightDimActive = false;
// Generic Settings-page persistence queue, drained by networkTask (core 0)
// so the flash write never happens on the render core.
volatile bool pendingConfigSave = false;
volatile uint8_t pendingConfigKeyId = 0;
volatile int32_t pendingConfigValue = 0;
volatile bool pendingForgetWifi = false;
volatile bool pendingRestart = false;  // see state.h -- drained by networkTask, not called directly
int cfgScreenRotation = 1;  // 1 = normal, 3 = flipped 180 (see settings.cpp's Rotation)

// Both thresholds below are WALL-CLOCK, deliberately not counted in poll
// cycles: Poll Interval is settable from 5s to 5min and Battery Save floors it
// to 120s, so a cycle count silently divides any threshold by the interval
// (the CYD's old "3 cycles" offline trigger was a 15-second hair trigger at
// the 5s setting). millis() compared with unsigned subtraction handles the
// ~49-day rollover.

// millis() of the last poll that found WiFi down, 0 when WiFi is up.
uint32_t wifiDownSinceMs = 0;
// ~15 min of continuous WiFi loss -> self-reboot, at any poll interval.
const uint32_t RESTART_AFTER_WIFI_DOWN_MS = 900000UL;

// millis() of the last *successful* poll. Once this is more than
// OFFLINE_AFTER_MS in the past, STATE.haveData is forced back to false,
// switching the display to the cat/offline screen. Left at 0 on purpose: a
// board that never reaches the Mac goes offline OFFLINE_AFTER_MS after boot.
uint32_t lastFetchSuccessMs = 0;
const uint32_t OFFLINE_AFTER_MS = 60000UL;  // ~1 min without a successful poll

// ── NETWORK TASK ───────────────────────────────────────────
// All blocking I/O (usage poll -- which also carries BTC/weather -- mDNS,
// WiFi reconnect, SD writes) runs here on core 0, so loop() on core 1 keeps
// sampling touch and repainting even while a fetch stalls. Only the brief
// STATE copies inside the fetch helpers take stateMutex.
void networkTask(void* param) {
  uint32_t lastUsagePollMs = 0;
  uint32_t lastHeapLogMs = 0;
  bool lowHeapLogged = false;
  for (;;) {
    uint32_t now = millis();

    // Black-box heartbeat: free internal heap + uptime to /diag_log.csv
    // hourly, plus a one-shot warning the first time it dips below 20KB.
    uint32_t freeInternal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    if (now - lastHeapLogMs >= 3600000UL) {
      lastHeapLogMs = now;
      logDiag((String("heap free=") + freeInternal + " psram_free=" + ESP.getFreePsram() +
               " uptime_min=" + (now / 60000)).c_str());
    }
    if (freeInternal < 20000 && !lowHeapLogged) {
      lowHeapLogged = true;
      logDiag((String("low_heap free=") + freeInternal).c_str());
    }

    if (now - lastUsagePollMs >= POLL_INTERVAL_MS) {
      lastUsagePollMs = now;
      lastPollMs = now;  // drives the footer progress line on the render side
      // FAT bookkeeping for Device Stats, paced by wall time, not poll rate.
      static uint32_t lastCapacityRefreshMs = 0;
      if (STATE.sdOk && (lastCapacityRefreshMs == 0 ||
                         now - lastCapacityRefreshMs >= SD_PERSIST_MIN_MS)) {
        lastCapacityRefreshMs = now;
        refreshSdCapacityCache();
      }
      if (WiFi.status() == WL_CONNECTED) {
        wifiOk = true;
        if (wifiDownSinceMs != 0) {
          logDiag(("wifi_recovered after " + String((now - wifiDownSinceMs) / 1000) + "s").c_str());
          wifiDownSinceMs = 0;
        }
        ensureMdns();
        connected = fetchUsage();
      } else {
        wifiOk = false;
        if (wifiDownSinceMs == 0) {
          wifiDownSinceMs = now;
          logDiag("wifi_down");
        }
        connected = false;
        mdnsStarted = false;       // re-init mDNS once WiFi returns
        WiFi.reconnect();
        if (now - wifiDownSinceMs >= RESTART_AFTER_WIFI_DOWN_MS) {
          logDiag("restart_wifi_timeout");
          ESP.restart();
        }
      }

      // A failed poll covers both WiFi down and WiFi up but the Mac
      // unreachable. haveData is otherwise sticky-true, so this is the only
      // thing that ever flips the display back to the offline/cat screen.
      if (connected) {
        STATE.haveData = true;
        lastFetchSuccessMs = now;
        pollOkSeq++;  // one pace sweep + one status-dot pulse on the render side
      } else if (now - lastFetchSuccessMs >= OFFLINE_AFTER_MS) {
        STATE.haveData = false;
      } else if (!STATE.haveData && STATE.sdOk) {
        // Still inside the grace window with nothing to show yet (cold boot
        // against an unreachable Mac): fall back to the SD cache.
        STATE.haveData = loadCachedUsage();
      }
    }

    if (pendingConfigSave) {
      pendingConfigSave = false;
      saveIntConfigToFlash(CONFIG_KEY_NAMES[pendingConfigKeyId], pendingConfigValue);
    }

    if (pendingForgetWifi) {
      pendingForgetWifi = false;
      forgetWifiFromFlash();
      ESP.restart();  // only after the erase above has actually landed in flash
    }

    if (pendingRestart) {
      pendingRestart = false;
      ESP.restart();  // drained here (core 0), never between this task's own SD ops
    }

    vTaskDelay(pdMS_TO_TICKS(100));
  }
}

// ── SCREEN SLEEP ───────────────────────────────────────────
// Tap the sleep button (corner slot a) -> the backlight fades to 0
// (motion.backlight.sleep), then panel DISPOFF + SLPIN, CPU down to 80MHz
// (lowest WiFi-safe clock; APB stays 80MHz so LEDC, SPI and I2C are
// unaffected), WiFi modem sleep, polls floored to the Battery Save cadence.
// loop() then only watches touch; the next press anywhere wakes and is
// swallowed. Nothing is presented while asleep, so the frame (and an open
// GIF's canvas) is exactly as it was on wake.
static const uint32_t SLEEP_LOOP_MS = 50;
static uint32_t sleepStartMs = 0;
static uint32_t cpuMhzBeforeSleep = 240;
static bool swallowUntilUp = false;  // the waking press never reaches the screen

void enterScreenSleep() {
  if (screenSleeping) return;
  // The fade is short and nothing on screen needs input during it.
  backlightFadeTo(0, TOK_MOTION_BACKLIGHT_SLEEP_MS);
  uint32_t t0 = millis();
  while (millis() - t0 < (uint32_t)(TOK_MOTION_BACKLIGHT_SLEEP_MS * motionTimeScale) + 20) {
    backlightTick(millis());
    delay(10);
  }
  screenSleeping = true;
  sleepStartMs = millis();
  navResetGesture();
  applyEffectiveBrightness();
  displaySetSleep(true);
  cpuMhzBeforeSleep = getCpuFrequencyMhz();
  setCpuFrequencyMhz(80);
  WiFi.setSleep(true);
  applyEffectivePoll();
  Serial.printf("[sleep] enter (cpu %lu MHz, poll %lus)\n", (unsigned long)getCpuFrequencyMhz(),
                (unsigned long)(POLL_INTERVAL_MS / 1000));
}

void exitScreenSleep() {
  if (!screenSleeping) return;
  setCpuFrequencyMhz(cpuMhzBeforeSleep);
  WiFi.setSleep(false);
  screenSleeping = false;
  applyEffectivePoll();
  displaySetSleep(false);
  bool catMode = navCatLayout();
  if (settingsScreen != SET_OFF) renderSettings();
  else if (catMode && !weatherPageOpen && !devicePageOpen) presentFrame();
  else render();
  // DISPON first, then the backlight fades up (motion.backlight.wake): the
  // frame is intact, so the screen is effectively there at once.
  applyEffectiveBrightness(TOK_MOTION_BACKLIGHT_WAKE_MS);
  Serial.printf("[sleep] exit after %lus\n", (unsigned long)((millis() - sleepStartMs) / 1000));
}

// ── SERIAL DEBUG HOOKS ─────────────────────────────────────
// Single-key commands over the USB serial port, so pages can be driven and
// screenshotted without touching the board (tools/grab_screen.py):
//   n / p   next / previous page        w / d / s   Weather / Device / Settings
//   x       close any sheet             g           grab: raw frame dump
//   z       toggle screen sleep         m           slow motion (x10) on/off
static void closeSheetNow() {
  if (navSheetOpen()) navCloseSheet();
  navFinishTransition();
}

static void serialCommand(char c) {
  switch (c) {
    case 'n': case 'p':
      closeSheetNow();
      navGoToPage(c == 'n' ? (currentPage + 1) % PAGE_COUNT : (currentPage - 1 + PAGE_COUNT) % PAGE_COUNT, c == 'n');
      break;
    case 'w': closeSheetNow(); navOpenSheet(0); break;
    case 'd': closeSheetNow(); navOpenSheet(1); break;
    case 's': closeSheetNow(); navOpenSheet(2); break;
    case 'x': navCloseSheet(); break;
    case 'z':
      if (screenSleeping) exitScreenSleep();
      else enterScreenSleep();
      break;
    case 'm':
      motionTimeScale = motionTimeScale > 1.0f ? 1.0f : 10.0f;
      Serial.printf("[motion] time scale x%.0f\n", motionTimeScale);
      break;
    case 'g': {
      // Header line, raw 480x320 big-endian RGB565 (the sprite's own bytes),
      // then a trailer line. grab_screen.py syncs on the header.
      Serial.printf("\nS3SHOT %d %d\n", SCREEN_W, SCREEN_H);
      Serial.flush();
      const uint8_t* p = (const uint8_t*)frame.getBuffer();
      const size_t total = SCREEN_W * SCREEN_H * 2;
      for (size_t off = 0; off < total; off += 4096) {
        size_t n = min((size_t)4096, total - off);
        Serial.write(p + off, n);
      }
      Serial.flush();
      Serial.print("\nS3END\n");
      break;
    }
  }
}

// ── SETUP / LOOP ───────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  Serial.setTxBufferSize(16384);

  // Create before any fetch: applyUsageJson locks it even during setup's
  // single-threaded initial fetch (uncontended there).
  stateMutex = xSemaphoreCreateMutex();
  sdMutex = xSemaphoreCreateMutex();  // must exist before the first logDiag/SD access below
  randomSeed(esp_random());           // so the cat picked on the cat pages differs each boot

  if (!displayBegin()) Serial.println("[display] panel init FAILED");
  touchBegin();
  fontsBegin();

  frame.setPsram(true);
  frame.setColorDepth(16);
  if (!frame.createSprite(SCREEN_W, SCREEN_H)) {
    Serial.println("[frame] sprite alloc FAILED -- restarting");
    delay(2000);
    ESP.restart();
  }
  frame.fillScreen(TOK_COLOR_BG_CANVAS);
  presentFrame();
  // Backlight only after a real frame is on the panel (never light raw GRAM).
  backlightFadeTo(200, 0);  // provisional; re-applied from flash once loadRuntimeConfig() runs

  // TF card on SD_MMC, 1-bit (the board wires CLK/CMD/D0 only). Attempted
  // once: SD I/O is blocking, so a flaky card must not stall the poll loop.
  SD_MMC.setPins(S3_SD_CLK, S3_SD_CMD, S3_SD_D0);
  STATE.sdOk = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_HIGHSPEED);
  if (!STATE.sdOk) STATE.sdOk = SD_MMC.begin("/sdcard", true, false, SDMMC_FREQ_DEFAULT);
  refreshSdCapacityCache();  // seed Device Stats before the first render(); still single-threaded here

  // Settings/config load from internal flash (NVS) -- unconditional, no SD needed.
  loadRuntimeConfig();
  displaySetFlipped(cfgScreenRotation == 3);
  // AUTO resumes wherever the swipe cycle was before the last restart.
  currentPage = (cfgBootPage == BOOT_PAGE_AUTO) ? cfgLastPage : cfgBootPage;
  applyEffectiveBrightness();  // honor brightness (+ night mode) from flash
  if (STATE.sdOk) {
    loadEnvCache();     // show last-known BTC/weather immediately, before any live fetch
    loadWeatherCache(); // full Weather-page snapshot (hourly/daily) if present
    showBootSplash();   // optional /splash.bmp, briefly, before the WiFi spinner
    scanCats();         // index /cats/*.gif for the cat GIF player page
    scanMovies();       // index /movies/*.mjpeg for the movie player page
  } else {
    Serial.println("[sd] card not found or failed to mount");
  }
  logDiag((String("boot reason=") + resetReasonStr()).c_str());

  // First-boot config portal: only when no WiFi SSID has ever been configured
  // (WIFI_SSID blank in config.h and none saved to flash).
  if (cfgWifiSsid.length() == 0) {
    runApSetup();  // blocks until configured, then restarts -- never returns
  }

  connectWifi();
  connected = fetchUsage();  // also carries BTC + weather from the Mac
  STATE.haveData = connected;
  if (connected) lastFetchSuccessMs = millis();  // start the offline grace window
  if (!connected && STATE.sdOk) STATE.haveData = loadCachedUsage();
  lastPollMs = millis();
  render();

  uint32_t flashUsed, flashTotal, ramUsed, ramTotal, psUsed, psTotal;
  Serial.printf("[device] flash=%d%% (%lu/%lu B) internal_ram=%d%% (%lu/%lu B) psram=%d%% (%lu/%lu B) TE=%lu\n",
                flashPercent(flashUsed, flashTotal), (unsigned long)flashUsed, (unsigned long)flashTotal,
                staticRamPercent(ramUsed, ramTotal), (unsigned long)ramUsed, (unsigned long)ramTotal,
                psramPercent(psUsed, psTotal), (unsigned long)psUsed, (unsigned long)psTotal,
                (unsigned long)displayTeCount());

  // Hand all blocking network I/O to core 0; loop() stays on core 1.
  xTaskCreatePinnedToCore(networkTask, "net", 8192, nullptr, 1, nullptr, 0);
}

// Target render-loop period: ~30 passes/sec, which is what the springs, the
// between-render top-ups and touch sampling run at. The GIF player paces
// itself inside that from each frame's own delay.
static const uint32_t LOOP_PERIOD_MS = 33;

void loop() {
  uint32_t loopStartUs = micros();
  uint32_t teWaitStart = teWaitAccumUs;
  uint32_t now = millis();

  while (Serial.available()) serialCommand((char)Serial.read());
  backlightTick(now);

  if (screenSleeping) {
    // Asleep: no drawing, no presents, no GIF decode -- just watch for a
    // press. The waking press is swallowed (it never reaches the screen).
    int32_t sx = 0, sy = 0;
    bool down = touchRead(sx, sy);
    if (down && !touchWasDown && now - sleepStartMs > TOK_TOUCH_WAKE_GUARD_MS) {
      Serial.printf("[touch] wake x=%ld y=%ld\n", (long)sx, (long)sy);
      lastTouchMs = now;
      swallowUntilUp = true;
      exitScreenSleep();
    }
    touchWasDown = down;
    delay(SLEEP_LOOP_MS);
    return;
  }

  // Cats own the screen on the cat pages, AND whenever offline.
  bool offline = !STATE.haveData;
  bool catMode = navCatLayout();
  navSyncCatMode(catMode);

  pixelShiftTick(now);

  // Night mode: own 1s timer, regardless of page/catMode/settings state; only
  // touches brightness on a transition edge, as a 2s fade.
  if (cfgNightModeOn) {
    static uint32_t lastNightCheckMs = 0;
    if (now - lastNightCheckMs >= 1000) {
      lastNightCheckMs = now;
      struct tm ti;
      if (getLocalTime(&ti, 0)) {
        bool inWindow = (ti.tm_hour >= 23 || ti.tm_hour < 7);
        if (inWindow != nightDimActive) {
          nightDimActive = inWindow;
          applyEffectiveBrightness(TOK_MOTION_BACKLIGHT_NIGHT_MS);
        }
      }
    }
  }

  // Hourly signal: one backlight breath at hh:00 (motion.backlight.breath) --
  // costs no presents, and never flashes or inverts the screen.
  if (cfgHourlyFlash) {
    static int lastBreathHour = -1;
    static uint32_t lastHourCheckMs = 0;
    if (now - lastHourCheckMs >= 500) {
      lastHourCheckMs = now;
      struct tm ti;
      if (getLocalTime(&ti, 0) && ti.tm_min == 0 && ti.tm_sec < 5 && ti.tm_hour != lastBreathHour) {
        lastBreathHour = ti.tm_hour;
        backlightBreath();
      }
    }
  }

  bool needPresent = shiftDirty;

  if (navSheetOpen()) {
    // Sheets are drawn on entry / change. Device Stats is live (1 Hz); a
    // Settings toast is cleared when it expires.
    static uint32_t lastSheetRenderMs = 0;
    if (devicePageOpen && !navTransitionActive() && now - lastSheetRenderMs >= 1000) {
      lastSheetRenderMs = now;
      render();
      needPresent = false;
    }
    if (settingsScreen != SET_OFF && toastText[0] && (int32_t)(now - toastUntilMs) >= 0) {
      toastText[0] = 0;
      renderSettings();
    }
  } else if (catMode) {
    if (gifTick(offline)) needPresent = true;
    if (currentPage == MIXED_PAGE && !offline) {
      static uint32_t lastMixedRenderMs = 0;
      if (now - lastMixedRenderMs >= 1000) {
        lastMixedRenderMs = now;
        lockState();
        drawMixedPageStatic();
        unlockState();
        gifPlayerRedrawOverlays(offline);
        needPresent = false;
      }
      if (shineTick(now)) needPresent = true;
      if (progressTick(now)) needPresent = true;
      if (pulseTick(now)) needPresent = true;
    }
  } else {
    // Repaint once a second for the local countdowns and the clock's second
    // hand (motion.ambient.maxHz); the sweep, pulse and hairline top up
    // between renders.
    static uint32_t lastRenderMs = 0;
    if (now - lastRenderMs >= 1000) {
      lastRenderMs = now;
      render();
      needPresent = false;
    } else {
      if (progressTick(now)) needPresent = true;
      if (shineTick(now)) needPresent = true;
      if (pulseTick(now)) needPresent = true;
    }
  }
  if (needPresent) presentFrame();

  int32_t tx = 0, ty = 0;
  bool touchDown = touchRead(tx, ty);
  if (swallowUntilUp) {
    if (!touchDown) swallowUntilUp = false;
  } else {
    navTouch(touchDown, tx, ty, now);
  }
  touchWasDown = touchDown;
  navTick(millis());

  // Duty-cycle CPU estimate: busy time this pass (minus idle TE waits) vs the
  // loop period, smoothed with an EMA.
  uint32_t busyUs = micros() - loopStartUs;
  uint32_t waitUs = teWaitAccumUs - teWaitStart;
  uint32_t workUs = busyUs > waitUs ? busyUs - waitUs : 0;
  uint32_t periodUs = max(busyUs, LOOP_PERIOD_MS * 1000);
  float sample = (float)workUs / periodUs * 100.0f;
  cpuPercentAvg = cpuPercentAvg * 0.9f + sample * 0.1f;

  uint32_t busyMs = busyUs / 1000;
  delay(busyMs < LOOP_PERIOD_MS ? LOOP_PERIOD_MS - busyMs : 1);
}
