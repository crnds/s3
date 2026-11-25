// sim/main.js -- loop(), screen sleep and boot (src/main.cpp).

let lastNightCheckMs = 0;
// ── SCREEN SLEEP (main.cpp twin) ──────────────────────────────
let swallowUntilUp = false;
function enterScreenSleep() {
  if (STATE.screenSleeping) return;
  displaySetInvert(false);  // never sleep with the panel mid-flash
  backlightFadeTo(0, TOK_MOTION_BACKLIGHT_SLEEP_MS);
  STATE.screenSleeping = true;
  STATE.sleepStartMs = performance.now();
  NAV.G = null; NAV.wasDown = false; NAV.pressedId = null;
  restartPollTimer();
  setPageStatus('SLEEP (tap to wake)');
}
function exitScreenSleep() {
  if (!STATE.screenSleeping) return;
  STATE.screenSleeping = false;
  restartPollTimer();
  redrawScreen();
  applyEffectiveBrightness(TOK_MOTION_BACKLIGHT_WAKE_MS);
}
// ── LOOP (main.cpp's loop(), ~30 passes/sec) ──────────────────
let lastRenderMs = 0, lastSheetRenderMs = 0, lastAmbientMs = 0;
function tick() {
  const start = performance.now(), now = start;
  backlightTick(now);
  if (!STATE.screenSleeping) {
    if (SHIFT_STEP_MS > 0 && now - STATE.lastShiftMs >= SHIFT_STEP_MS) {
      STATE.lastShiftMs = now;
      STATE.shiftIdx = (STATE.shiftIdx + 1) % 8;
      [STATE.shiftX, STATE.shiftY] = SHIFT_ORBIT[STATE.shiftIdx];
      STATE.shiftDirty = true;
    }
    if (now - lastNightCheckMs >= 1000) {
      lastNightCheckMs = now;
      const bkk = bangkokParts();
      if (STATE.cfgNightModeOn) {
        const hour = parseInt(bkk.hour, 10);
        const inWindow = hour >= 23 || hour < 7;
        if (inWindow !== STATE.nightDimActive) {
          STATE.nightDimActive = inWindow;
          applyEffectiveBrightness(TOK_MOTION_BACKLIGHT_NIGHT_MS);
        }
      }
    }
    // Hourly signal (main.cpp twin): inverted on the even seconds of hh:00:00..05.
    {
      let invertNow = false;
      if (STATE.cfgHourlyFlash) {
        const bkk = bangkokParts();
        invertNow = parseInt(bkk.minute, 10) === 0 && parseInt(bkk.second, 10) < 6 && parseInt(bkk.second, 10) % 2 === 0;
      }
      if (hourlyTestStartMs) {
        const el = now - hourlyTestStartMs;
        if (el < 6000) invertNow = Math.floor(el / 1000) % 2 === 0;
        else hourlyTestStartMs = 0;
      }
      displaySetInvert(invertNow);
    }
    let needPresent = STATE.shiftDirty;
    if (navSheetOpen()) {
      if (STATE.devicePageOpen && !navTransitionActive() && now - lastSheetRenderMs >= 1000) {
        lastSheetRenderMs = now;
        render();
        needPresent = false;
      }
      if (STATE.settingsScreen !== 'off' && toastText && now >= toastUntilMs) { toastText = ''; renderSettings(); }
    } else if (now - lastRenderMs >= 1000) {
      lastRenderMs = now;
      render();
      needPresent = false;
      lastAmbientMs = now;
    } else if ((!navCatLayout() || catPanePage(!STATE.haveData)) &&
               now - lastAmbientMs >= TOK_MOTION_AMBIENT_TICK_MS * motionTimeScale) {
      // Ambient motion on one shared 8 Hz tick (main.cpp twin, motion.ambient.tick).
      lastAmbientMs = now;
      if (progressTick(now)) needPresent = true;
      if (shineTick(now)) needPresent = true;
      if (pulseTick(now)) needPresent = true;
    }
    if (needPresent) presentFrame();
  }
  processTouches();
  if (!STATE.screenSleeping) navTick(performance.now());
  setPageStatus(describeScreen());
  const busyMs = performance.now() - start;
  STATE.cpuPercentAvg = STATE.cpuPercentAvg * 0.9 + (busyMs / 33 * 100) * 0.1;
}

if (!window.S3_FONTS) {
  document.body.insertAdjacentHTML('afterbegin',
    '<p style="color:var(--warn)">sim/fonts.js missing -- run python3 tools/make_vlw.py</p>');
} else {
  const slow = parseFloat(new URLSearchParams(location.search).get('slowmo'));
  if (slow > 1) motionTimeScale = slow;
  if (FIXTURE_NAME) {
    applyEffectiveBrightness(0);
    startFixture(FIXTURE_NAME).catch(err => { window.simFixtureError = String(err); log(String(err), 'err'); });
  } else {
    initDataSource();
    loadWeatherCache();
    loadSettings();
    applyEffectiveBrightness(0);
    render();
    fetchUsage();
    restartPollTimer();
    setInterval(tick, 33);
  }
}
