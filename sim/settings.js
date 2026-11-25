// sim/settings.js -- SETTINGS table, the Settings screens and persistence (src/settings.cpp, sd_store.cpp).

// ── SETTINGS TABLE (mirrors settings.cpp's SETTINGS[]) ────────
// Night mode: fixed 23:00-07:00, dims to 25%. The backlight is the canvas
// brightness filter (the backlight animator in sim/motion.js).
function applyEffectiveBrightness(fadeMs = 0) {
  if (STATE.screenSleeping) { backlightFadeTo(0, 0); return; }
  backlightFadeTo(STATE.nightDimActive ? NIGHT_MODE_DIM_VALUE : STATE.cfgBrightness, fadeMs);
}
const BATTERY_SAVE_OFF = 0, BATTERY_SAVE_ON = 1, BATTERY_SAVE_AUTO = 2;
function batterySaveActive() {
  if (STATE.cfgBatterySaveMode === BATTERY_SAVE_ON) return true;
  if (STATE.cfgBatterySaveMode === BATTERY_SAVE_AUTO) return !!STATE.serverBatterySave;
  return false;
}
// settings.cpp's applyEffectivePoll(): the Poll interval preference with the
// Battery Save / screen-sleep floor. The firmware stores it in
// POLL_INTERVAL_MS; the sim returns it to each caller instead.
function applyEffectivePoll() {
  let sec = Number(document.getElementById('poll-interval').value) / 1000;
  if (!Number.isFinite(sec) || sec <= 0) sec = POLL_DEFAULT_MS / 1000;
  if ((batterySaveActive() || STATE.screenSleeping) && sec < BATTERY_SAVE_POLL_SEC) sec = BATTERY_SAVE_POLL_SEC;
  return sec * 1000;
}

function getCurrentBrightness() { return STATE.cfgBrightness; }
function applyBrightness(v) {
  STATE.cfgBrightness = v;
  lsSet(BRIGHTNESS_KEY, String(v));
  applyEffectiveBrightness(TOK_MOTION_BACKLIGHT_ADJUST_MS);
}
function getCurrentNone() { return -1; }
// Restart = page reload (STATE re-initializes, same as the board re-running setup()).
function applyRestart() { setTimeout(() => location.reload(), 300); }
function applyForgetWifi() {
  console.log('[sim] Forget Wi-Fi: on hardware this erases wifi_ssid/wifi_password from flash and restarts.');
  setTimeout(() => location.reload(), 300);
}
function getCurrentPollInterval() { return Number(document.getElementById('poll-interval').value) / 1000; }
function applyPollInterval(v) {
  document.getElementById('poll-interval').value = String(v * 1000);
  restartPollTimer();
}
function getCurrentPixelShift() { return SHIFT_STEP_MS / 60000; }
function applyPixelShift(v) { SHIFT_STEP_MS = v * 60000; }
function getCurrentBootPage() { return STATE.cfgBootPage; }
function applyBootPage(v) {
  STATE.cfgBootPage = v;
  if (v !== BOOT_PAGE_AUTO) STATE.currentPage = v;
  lsSet(BOOT_PAGE_KEY, String(v));
}
function getCurrentCatShuffle() { return STATE.catShuffleSec; }
function applyCatShuffle(v) { STATE.catShuffleSec = v; lsSet(CAT_SHUFFLE_KEY, String(v)); }
function getCurrentBatterySave() { return STATE.cfgBatterySaveMode; }
function applyBatterySave(v) {
  STATE.cfgBatterySaveMode = Math.max(BATTERY_SAVE_OFF, Math.min(BATTERY_SAVE_AUTO, v));
  lsSet(BATTERY_SAVE_KEY, String(STATE.cfgBatterySaveMode));
  restartPollTimer();
}
// Rotation: the board flips its software rotate (display.cpp) and touch
// mapping; a 180deg flip changes nothing about layout, so the sim only
// persists the value.
function getCurrentRotation() { return STATE.cfgScreenRotation; }
function applyRotation(v) { STATE.cfgScreenRotation = v; lsSet(ROTATION_KEY, String(v)); }
function getCurrentNightMode() { return STATE.cfgNightModeOn ? 1 : 0; }
function applyNightMode(v) {
  STATE.cfgNightModeOn = v !== 0;
  lsSet(NIGHT_MODE_KEY, String(v));
  if (!STATE.cfgNightModeOn && STATE.nightDimActive) {
    STATE.nightDimActive = false;
    applyEffectiveBrightness(TOK_MOTION_BACKLIGHT_NIGHT_MS);
  }
}
function getCurrentShowCountdown() { return STATE.cfgShowCountdown ? 1 : 0; }
function applyShowCountdown(v) { STATE.cfgShowCountdown = v !== 0; lsSet(SHOW_COUNTDOWN_KEY, String(v)); }
function getCurrentShowAqi() { return STATE.cfgShowAqi ? 1 : 0; }
function applyShowAqi(v) { STATE.cfgShowAqi = v !== 0; lsSet(SHOW_AQI_KEY, String(v)); }
function getCurrentHourlyFlash() { return STATE.cfgHourlyFlash ? 1 : 0; }
function applyHourlyFlash(v) { STATE.cfgHourlyFlash = v !== 0; lsSet(HOURLY_FLASH_KEY, String(v)); }
// ── Sound section (settings.cpp twin) ──
// Two volumes: Tap (the tock) and Alert (chime, Claude ding, alerts).
const VOLUME_LABELS = ['Off', '1', '2', '3', '4', '5', '6', '7'];
function getCurrentTapVol() { return STATE.cfgTapVol; }
function applyTapVol(v) { STATE.cfgTapVol = Math.max(0, Math.min(SOUND_VOL_MAX, v)); lsSet(TAP_VOL_KEY, String(STATE.cfgTapVol)); }
function getCurrentAlertVol() { return STATE.cfgAlertVol; }
function applyAlertVol(v) { STATE.cfgAlertVol = Math.max(0, Math.min(SOUND_VOL_MAX, v)); lsSet(ALERT_VOL_KEY, String(STATE.cfgAlertVol)); }
// Parent row text: "Tap 2, Alert 4", or "Off" when both are silent.
function soundSummary() {
  if (STATE.cfgTapVol === 0 && STATE.cfgAlertVol === 0) return 'Off';
  return 'Tap ' + VOLUME_LABELS[STATE.cfgTapVol] + ', Alert ' + VOLUME_LABELS[STATE.cfgAlertVol];
}
let testSoundNext = 0;
const TEST_LABELS = ['Tap', 'Done', 'Attention', 'Alert', 'Chime'];
function getCurrentTestSound() { return testSoundNext; }
function applyTestSound(v) { console.log('[sim] test sound: ' + TEST_LABELS[v] + ' (silent in the sim)'); testSoundNext = (v + 1) % TEST_LABELS.length; }
// One get/apply pair per persisted On/Off switch.
function boolSetting(field, key) {
  return { getCurrent: () => (STATE[field] ? 1 : 0), apply: v => { STATE[field] = v !== 0; lsSet(key, String(v)); } };
}
function getCurrentShowProgress() { return STATE.cfgShowProgress ? 1 : 0; }
function applyShowProgress(v) { STATE.cfgShowProgress = v !== 0; lsSet(SHOW_PROGRESS_KEY, String(v)); }
function getCurrentReduceMotion() { return STATE.cfgReduceMotion ? 1 : 0; }
function applyReduceMotion(v) { STATE.cfgReduceMotion = v !== 0; lsSet(REDUCE_MOTION_KEY, String(v)); }
function getCurrentTheme() { return STATE.cfgLightMode ? 1 : 0; }
function applyTheme(v) {
  STATE.cfgLightMode = v !== 0;
  applyThemeTokens();
  navRefreshBehind();
  lsSet(LIGHT_MODE_KEY, String(v));
}
function getCurrentHighContrast() { return STATE.cfgHighContrast ? 1 : 0; }
function applyHighContrast(v) { STATE.cfgHighContrast = v !== 0; applyContrast(); lsSet(HIGH_CONTRAST_KEY, String(v)); }

const ROW_NAV = 0, ROW_TOGGLE = 1, ROW_ACTION = 2, ROW_SECTION = 3, ROW_BUTTON = 4;
const BRIGHTNESS_VALUES = [0, 64, 128, 191, 255];
const POLL_VALUES = [5, 10, 20, 60, 300];
const ONOFF = { cols: 2, count: 2, values: [0, 1], valueLabels: ['Off', 'On'] };
const SOUND_ROWS = [
  { label: 'Tap volume', subtitle: 'Volume of the tap tock', hint: '1 is faint, 7 is loud. Off silences taps',
    kind: ROW_NAV, cols: 4, count: 8, values: [0, 1, 2, 3, 4, 5, 6, 7],
    valueLabels: VOLUME_LABELS, getCurrent: getCurrentTapVol, apply: applyTapVol },
  { label: 'Alert volume', subtitle: 'Volume of the chime, Claude ding and alerts', hint: '1 is faint, 7 is loud. Off silences them',
    kind: ROW_NAV, cols: 4, count: 8, values: [0, 1, 2, 3, 4, 5, 6, 7],
    valueLabels: VOLUME_LABELS, getCurrent: getCurrentAlertVol, apply: applyAlertVol },
  { label: 'Play test sound', subtitle: '', hint: '', kind: ROW_BUTTON, cols: 5, count: 5, values: [0, 1, 2, 3, 4],
    valueLabels: TEST_LABELS, getCurrent: getCurrentTestSound, apply: applyTestSound },
  { label: 'Hourly chime', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF, ...boolSetting('cfgHourlyChime', HOURLY_CHIME_KEY) },
  { label: 'Claude ding', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF, ...boolSetting('cfgClaudeDing', CLAUDE_DING_KEY) },
  { label: 'Alerts', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF, ...boolSetting('cfgSoundAlerts', SOUND_ALERTS_KEY) },
  { label: 'Mute when asleep', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF, ...boolSetting('cfgMuteSleep', MUTE_SLEEP_KEY) },
  { label: 'Mute at night', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF, ...boolSetting('cfgMuteNight', MUTE_NIGHT_KEY) },
];
// Settings > Firmware (settings.cpp twin). The browser can't install a build:
// it reads as a board already running whatever the server offers -- a
// deliberate non-twin of the install, like the speaker.
function firmwareSummary() { return STATE.firmwareOffer ? 'Up to date' : 'None published'; }
function applyUpdateNow() {
  toastText = STATE.firmwareOffer ? 'Already up to date' : 'No build published';
  renderSettings();
}
const FIRMWARE_ROWS = [
  { label: 'Auto update', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF, ...boolSetting('cfgOtaAuto', OTA_AUTO_KEY) },
  { label: 'Update now', subtitle: 'Install the build the Mac offers', hint: 'Tap twice to update and restart',
    kind: ROW_ACTION, cols: 1, count: 1, values: [0], valueLabels: ['Update'],
    getCurrent: getCurrentNone, apply: applyUpdateNow, doneToast: 'Updating...' },
];
const SETTINGS = [
  { label: 'Brightness', subtitle: 'Backlight brightness', hint: '', kind: ROW_NAV, cols: 5, count: 5,
    values: BRIGHTNESS_VALUES, valueLabels: ['0%', '25%', '50%', '75%', '100%'],
    getCurrent: getCurrentBrightness, apply: applyBrightness },
  { label: 'Night mode', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF,
    getCurrent: getCurrentNightMode, apply: applyNightMode },
  { label: 'Rotation', subtitle: 'For upside-down mounting', hint: '', kind: ROW_NAV, cols: 2, count: 2,
    values: [1, 3], valueLabels: ['Normal', 'Flipped'], getCurrent: getCurrentRotation, apply: applyRotation },
  { label: 'Theme', subtitle: 'Dark or light appearance', hint: '', kind: ROW_NAV, cols: 2, count: 2,
    values: [0, 1], valueLabels: ['Dark', 'Light'], getCurrent: getCurrentTheme, apply: applyTheme },
  { label: 'Boot page', subtitle: 'Page shown after a restart', hint: 'Auto resumes the last page shown',
    // 'Mixed' is Status + cats: its full name (110px) fits only a 3-column cell.
    kind: ROW_NAV, cols: 4, count: 8, values: [BOOT_PAGE_AUTO, 0, 1, 2, 3, 4, 5, 6],
    valueLabels: ['Auto', 'Status', 'Usage', 'Cats', 'Movies', 'Mixed', 'Note', 'Overview'],
    getCurrent: getCurrentBootPage, apply: applyBootPage },
  { label: 'Cat shuffle', subtitle: 'How long each cat plays', hint: 'Off plays each cat to its end; Fixed keeps one',
    kind: ROW_NAV, cols: 5, count: 5, values: [-1, 0, 5, 10, 30], valueLabels: ['Fixed', 'Off', '5s', '10s', '30s'],
    getCurrent: getCurrentCatShuffle, apply: applyCatShuffle },
  { label: 'Pace bars', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF,
    getCurrent: getCurrentShowCountdown, apply: applyShowCountdown },
  { label: 'Show AQI', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF,
    getCurrent: getCurrentShowAqi, apply: applyShowAqi },
  { label: 'Hourly signal', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF,
    getCurrent: getCurrentHourlyFlash, apply: applyHourlyFlash },
  { label: 'Sound', subtitle: '', hint: '', kind: ROW_SECTION, cols: 0, count: 0,
    children: SOUND_ROWS, summary: soundSummary },
  { label: 'Poll progress', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF,
    getCurrent: getCurrentShowProgress, apply: applyShowProgress },
  { label: 'Poll interval', subtitle: 'How often to fetch /api/usage', hint: '', kind: ROW_NAV, cols: 5, count: 5,
    values: POLL_VALUES, valueLabels: ['5s', '10s', '20s', '60s', '5m'],
    getCurrent: getCurrentPollInterval, apply: applyPollInterval },
  { label: 'Battery save', subtitle: 'Polls at most every 2 min while on', hint: "Auto follows the Mac's power state",
    kind: ROW_NAV, cols: 3, count: 3, values: [0, 1, 2], valueLabels: ['Off', 'On', 'Auto'],
    getCurrent: getCurrentBatterySave, apply: applyBatterySave },
  { label: 'Pixel shift', subtitle: 'Anti-retention orbit interval', hint: '', kind: ROW_NAV, cols: 4, count: 4,
    values: [0, 1, 3, 10], valueLabels: ['Off', '1m', '3m', '10m'],
    getCurrent: getCurrentPixelShift, apply: applyPixelShift },
  { label: 'Firmware', subtitle: '', hint: '', kind: ROW_SECTION, cols: 0, count: 0,
    children: FIRMWARE_ROWS, summary: firmwareSummary },
  { label: 'Reduce motion', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF,
    getCurrent: getCurrentReduceMotion, apply: applyReduceMotion },
  { label: 'Increase contrast', subtitle: '', hint: '', kind: ROW_TOGGLE, ...ONOFF,
    getCurrent: getCurrentHighContrast, apply: applyHighContrast },
  { label: 'Forget Wi-Fi', subtitle: 'Erase the saved network and reboot into setup', hint: 'Tap twice to erase Wi-Fi',
    kind: ROW_ACTION, cols: 1, count: 1, values: [0], valueLabels: ['Forget Wi-Fi'],
    getCurrent: getCurrentNone, apply: applyForgetWifi, doneToast: 'Wi-Fi forgotten, restarting...' },
  { label: 'Restart', subtitle: 'Reboot the board', hint: 'Tap twice to restart the board',
    kind: ROW_ACTION, cols: 1, count: 1, values: [0], valueLabels: ['Restart'],
    getCurrent: getCurrentNone, apply: applyRestart, doneToast: 'Restarting...' },
];
const CONFIRM_ARM_MS = TOK_TOUCH_ARM_WINDOW_MS;
let toastText = '', toastUntilMs = 0;

// ── SETTINGS (settings.cpp twin) ──────────────────────────────
const LIST_X = TOK_LAYOUT_CONTENT_X0, LIST_W = TOK_LAYOUT_CONTENT_W;
const LIST_VIEW_Y0 = TOK_LAYOUT_HEADER_H, LIST_ROW0_Y = TOK_LAYOUT_HEADER_CONTENT_Y;
const LIST_STEP = TOK_LIST_ROW_H + TOK_SPACE_LIST_GAP;
const LIST_TRAIL_R = LIST_X + LIST_W - TOK_LIST_ROW_INSET;
// The rows the list / leaf screens index into: the main list or the open
// section's children (settings.cpp's rows()/rowCount()).
let mainScroll = 0, leftSection = -1, leftScroll = 0;
function rows() { return STATE.settingsSection < 0 ? SETTINGS : SETTINGS[STATE.settingsSection].children; }
function rowCount() { return rows().length; }
function settingsEnterSection() {
  mainScroll = STATE.settingsScrollOffset;
  STATE.settingsSection = STATE.settingsLeafIndex;
  STATE.settingsScrollOffset = 0;
  STATE.confirmArmedRow = -1;
}
function settingsLeaveSection() {
  leftSection = STATE.settingsSection; leftScroll = STATE.settingsScrollOffset;
  STATE.settingsSection = -1;
  STATE.settingsScrollOffset = mainScroll;
  STATE.confirmArmedRow = -1;
}
function settingsUndoLeaveSection() {
  mainScroll = STATE.settingsScrollOffset;
  STATE.settingsSection = leftSection;
  STATE.settingsScrollOffset = leftScroll;
}
function settingsScrollMax() { return Math.max(0, LIST_ROW0_Y + rowCount() * LIST_STEP - SCREEN_H); }
function rowY(idx) { return LIST_ROW0_Y + idx * LIST_STEP - STATE.settingsScrollOffset; }
function currentValueText(d) {
  const cur = d.getCurrent();
  for (let i = 0; i < d.count; i++) if (d.values[i] === cur) return d.valueLabels[i];
  if (d.values === BRIGHTNESS_VALUES) return Math.trunc((cur * 100 + 127) / 255) + '%';
  if (d.values === POLL_VALUES) return cur + 's';
  return '--';
}
function drawTogglePill(xRight, cy, on) {
  const x = xRight - TOK_TOGGLE_W, y = cy - TOK_TOGGLE_H / 2;
  gfx.fillSmoothRoundRect(x, y, TOK_TOGGLE_W, TOK_TOGGLE_H, TOK_TOGGLE_H / 2, on ? TOK_COLOR_ACCENT : TOK_COLOR_FILL_TRACK);
  drawTextC(TOK_TYPE_HEADLINE, x + TOK_TOGGLE_W / 2, cy - Math.trunc(fontLineH(TOK_TYPE_HEADLINE) / 2), on ? 'On' : 'Off',
            on ? TOK_COLOR_TEXT_ON_ACCENT : TOK_COLOR_TEXT_SECONDARY);
}
// Scroll-edge shade: rows y 44..51 at material.scrim once scrolled.
function shadeRows(y0, y1) {
  const img = ctx.getImageData(0, y0, SCREEN_W, y1 - y0);
  scrimImage(img, 2);
  ctx.putImageData(img, 0, y0);
}
function drawSettingsList() {
  gfx.fillScreen(TOK_COLOR_BG_CANVAS);
  gfx.setClipRect(0, LIST_VIEW_Y0, SCREEN_W, SCREEN_H - LIST_VIEW_Y0);
  for (let idx = 0; idx < rowCount(); idx++) {
    const d = rows()[idx];
    const y = rowY(idx);
    if (y + TOK_LIST_ROW_H < LIST_VIEW_Y0 || y >= SCREEN_H) continue;
    const pressed = NAV.pressedId === 'row' && NAV.pressedIndex === idx;
    drawCardSurface(LIST_X, y, LIST_W, TOK_LIST_ROW_H, pressed ? TOK_COLOR_SURFACE_RAISED : TOK_COLOR_SURFACE_CARD);
    const cy = y + TOK_LIST_ROW_H / 2;
    drawText(TOK_TYPE_HEADLINE, LIST_X + TOK_LIST_ROW_INSET, cy - Math.trunc(fontLineH(TOK_TYPE_HEADLINE) / 2), d.label,
             d.kind === ROW_ACTION ? TOK_COLOR_STATUS_ERROR
             : d.kind === ROW_BUTTON ? TOK_COLOR_ACCENT : TOK_COLOR_TEXT_PRIMARY);
    if (d.kind === ROW_TOGGLE) {
      drawTogglePill(LIST_TRAIL_R, cy, d.getCurrent() !== 0);
    } else if (d.kind === ROW_BUTTON) {
      // Play glyph (accent) with the sound that plays next beside it.
      const gx = LIST_TRAIL_R - 8;
      gfx.fillTriangle(gx - 5, cy - 7, gx - 5, cy + 7, gx + 6, cy, TOK_COLOR_ACCENT);
      drawTextR(TOK_TYPE_BODY, gx - 5 - TOK_SPACE_MD, cy - Math.trunc(fontLineH(TOK_TYPE_BODY) / 2),
                currentValueText(d), TOK_COLOR_TEXT_SECONDARY);
    } else if (d.kind === ROW_SECTION) {
      const chevCx = LIST_TRAIL_R - 4;
      drawChevron(chevCx, cy, TOK_COLOR_TEXT_TERTIARY);
      if (d.summary)
        drawTextR(TOK_TYPE_BODY, chevCx - 2 - TOK_SPACE_SM, cy - Math.trunc(fontLineH(TOK_TYPE_BODY) / 2),
                  d.summary(), TOK_COLOR_TEXT_SECONDARY);
    } else {
      const chevCx = LIST_TRAIL_R - 4;
      drawChevron(chevCx, cy, TOK_COLOR_TEXT_TERTIARY);
      if (d.kind === ROW_NAV)
        drawTextR(TOK_TYPE_BODY, chevCx - 2 - TOK_SPACE_SM, cy - Math.trunc(fontLineH(TOK_TYPE_BODY) / 2),
                  currentValueText(d), TOK_COLOR_TEXT_SECONDARY);
    }
  }
  gfx.clearClipRect();
  if (STATE.settingsScrollOffset > 0) shadeRows(LIST_VIEW_Y0, LIST_ROW0_Y);
  const maxS = settingsScrollMax();
  if (maxS > 0) {
    const trackY = LIST_ROW0_Y, trackH = TOK_LAYOUT_OVERLAY_CONTENT_Y1 - LIST_ROW0_Y;
    const contentH = rowCount() * LIST_STEP;
    const thumbH = Math.max(24, Math.trunc(trackH * trackH / contentH));
    const off = Math.max(0, Math.min(maxS, STATE.settingsScrollOffset));
    const thumbY = trackY + Math.trunc((trackH - thumbH) * off / maxS);
    gfx.fillSmoothRoundRect(LIST_X + LIST_W - 6, thumbY, 4, thumbH, 2, TOK_COLOR_TEXT_TERTIARY);
  }
  drawModalHeader(STATE.settingsSection >= 0, STATE.settingsSection < 0 ? 'Settings' : SETTINGS[STATE.settingsSection].label,
                  NAV.pressedId === 'close');
}
const CELL_W_BY_COLS = [0, 464, 228, 149, 110, 86];
function gridTop(d) {
  let y = TOK_LAYOUT_HEADER_CONTENT_Y;
  if (d.subtitle) y += fontLineH(TOK_TYPE_BODY) + TOK_SPACE_XL;
  return y;
}
function cellRect(d, i) {
  const row = i < d.cols ? 0 : 1;
  const n = row === 0 ? Math.min(d.cols, d.count) : d.count - d.cols;
  const k = row === 0 ? i : i - d.cols;
  const w = CELL_W_BY_COLS[n];
  const h = d.kind === ROW_ACTION ? TOK_BUTTON_LG : TOK_OPTION_CELL_H;
  const total = n * w + (n - 1) * TOK_SPACE_SM;
  return { x: TOK_LAYOUT_CONTENT_X0 + Math.trunc((TOK_LAYOUT_CONTENT_W - total) / 2) + k * (w + TOK_SPACE_SM),
           y: gridTop(d) + row * (TOK_OPTION_CELL_H + TOK_SPACE_SM), w, h };
}
function armedNow(now) {
  return STATE.confirmArmedRow === STATE.settingsLeafIndex && now - STATE.confirmArmedMs < CONFIRM_ARM_MS;
}
function drawToast() {
  if (!toastText || performance.now() >= toastUntilMs) return;
  const padX = TOK_SPACE_MD;
  const w = textW(TOK_TYPE_BODY, toastText) + 2 * padX;
  const x = Math.trunc((SCREEN_W - w) / 2), y = TOK_LAYOUT_OVERLAY_CONTENT_Y1 - TOK_TOAST_H;
  gfx.fillSmoothRoundRect(x, y, w, TOK_TOAST_H, TOK_RADIUS_SM, TOK_COLOR_SURFACE_RAISED);
  drawText(TOK_TYPE_BODY, x + padX, y + Math.trunc((TOK_TOAST_H - fontLineH(TOK_TYPE_BODY)) / 2), toastText, TOK_COLOR_TEXT_PRIMARY);
}
function drawSettingsLeaf() {
  const d = rows()[STATE.settingsLeafIndex];
  gfx.fillScreen(TOK_COLOR_BG_CANVAS);
  drawModalHeader(true, d.label, NAV.pressedId === 'close');
  if (d.subtitle) drawText(TOK_TYPE_BODY, TOK_LAYOUT_CONTENT_X0, TOK_LAYOUT_HEADER_CONTENT_Y, d.subtitle, TOK_COLOR_TEXT_SECONDARY);
  const current = d.kind === ROW_ACTION ? NaN : d.getCurrent();
  const armed = d.kind === ROW_ACTION && armedNow(performance.now());
  let bottom = 0;
  for (let i = 0; i < d.count; i++) {
    const { x, y, w, h } = cellRect(d, i);
    const pressed = NAV.pressedId === 'cell' && NAV.pressedIndex === i;
    let fill, ink, label = d.valueLabels[i];
    if (d.kind === ROW_ACTION) {
      fill = armed ? TOK_COLOR_STATUS_ERROR : (pressed ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_SURFACE_CARD);
      ink = armed ? TOK_COLOR_TEXT_ON_ACCENT : TOK_COLOR_STATUS_ERROR;
      if (armed) label = 'Tap again';
    } else {
      const sel = d.values[i] === current;
      fill = sel ? (pressed ? TOK_COLOR_ACCENT_PRESSED : TOK_COLOR_ACCENT) : (pressed ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_SURFACE_CARD);
      ink = sel ? TOK_COLOR_TEXT_ON_ACCENT : TOK_COLOR_TEXT_PRIMARY;
    }
    drawCardSurface(x, y, w, h, fill);
    drawTextC(TOK_TYPE_HEADLINE, x + Math.trunc(w / 2), y + Math.trunc((h - fontLineH(TOK_TYPE_HEADLINE)) / 2), label, ink);
    bottom = y + h;
  }
  if (d.hint) drawTextC(TOK_TYPE_CAPTION, SCREEN_W / 2, bottom + TOK_SPACE_LG, d.hint, TOK_COLOR_TEXT_SECONDARY);
  drawToast();
}
function drawSettingsScreen() {
  if (STATE.settingsScreen === 'leaf') drawSettingsLeaf(); else drawSettingsList();
  drawSystemCorner(false);
}
function renderSettings() { drawSettingsScreen(); presentFrame(); }
function settingsListHit(x, y) {
  if (y < LIST_VIEW_Y0 || x < LIST_X || x >= LIST_X + LIST_W) return -1;
  for (let idx = 0; idx < rowCount(); idx++) {
    const ry = rowY(idx);
    if (y >= ry && y < ry + TOK_LIST_ROW_H) return idx;
  }
  return -1;
}
// 'none' | 'leaf' | 'section' (settings.cpp's SettingsGo).
function settingsListActivate(idx) {
  const d = rows()[idx];
  if (!d) return 'none';
  if (d.kind === ROW_TOGGLE) { d.apply(d.getCurrent() ? 0 : 1); return 'none'; }
  if (d.kind === ROW_BUTTON) { d.apply(d.getCurrent()); return 'none'; }
  STATE.settingsLeafIndex = idx;
  STATE.confirmArmedRow = -1;
  return d.kind === ROW_SECTION ? 'section' : 'leaf';
}
function settingsLeafHit(x, y) {
  if (STATE.settingsLeafIndex < 0) return -1;
  const d = rows()[STATE.settingsLeafIndex];
  for (let i = 0; i < d.count; i++) {
    const r = cellRect(d, i);
    if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
  }
  return -1;
}
function settingsLeafActivate(i, now) {
  const d = rows()[STATE.settingsLeafIndex];
  if (d.kind === ROW_ACTION) {
    if (armedNow(now)) {
      STATE.confirmArmedRow = -1;
      toastText = d.doneToast;
      toastUntilMs = now + TOK_MOTION_TOAST_MS;
      renderSettings();
      d.apply(d.values[i]);
      return;
    }
    STATE.confirmArmedRow = STATE.settingsLeafIndex;
    STATE.confirmArmedMs = now;
    return;
  }
  d.apply(d.values[i]);
}

// ── SETTINGS PERSISTENCE ──────────────────────────────────────
function loadInt(key, fn) {
  const raw = lsGet(key);
  if (raw === null) return;
  const v = parseInt(raw, 10);
  if (!Number.isNaN(v)) fn(v);
}
// Projects and Limits merged into one Usage page, so every later page moved
// down one slot: saved boot / last page values of 2 and up lose one, once
// (src/sd_store.cpp's migratePageIndices()).
function migratePageIndices() {
  if (lsGet(PAGES_V2_KEY) !== null) return;
  for (const k of [BOOT_PAGE_KEY, LAST_PAGE_KEY]) {
    const v = parseInt(lsGet(k), 10);
    if (!Number.isNaN(v) && v >= 2) lsSet(k, String(v - 1));
  }
  lsSet(PAGES_V2_KEY, '1');
}
function loadSettings() {
  migratePageIndices();
  loadInt(BRIGHTNESS_KEY, v => { STATE.cfgBrightness = v; });
  loadInt(LAST_PAGE_KEY, v => { STATE.cfgLastPage = v; });
  loadInt(BOOT_PAGE_KEY, v => {
    STATE.cfgBootPage = v;
    STATE.currentPage = (v === BOOT_PAGE_AUTO) ? STATE.cfgLastPage : v;
  });
  loadInt(CAT_SHUFFLE_KEY, v => { STATE.catShuffleSec = v; });
  loadInt(NIGHT_MODE_KEY, v => { STATE.cfgNightModeOn = v !== 0; });
  loadInt(ROTATION_KEY, v => { STATE.cfgScreenRotation = v; });
  loadInt(SHOW_COUNTDOWN_KEY, v => { STATE.cfgShowCountdown = v !== 0; });
  loadInt(SHOW_AQI_KEY, v => { STATE.cfgShowAqi = v !== 0; });
  loadInt(HOURLY_FLASH_KEY, v => { STATE.cfgHourlyFlash = v !== 0; });
  loadInt(SHOW_PROGRESS_KEY, v => { STATE.cfgShowProgress = v !== 0; });
  loadInt(ALERT_VOL_KEY, v => { STATE.cfgAlertVol = Math.max(0, Math.min(SOUND_VOL_MAX, v)); });
  loadInt(TAP_VOL_KEY, v => { STATE.cfgTapVol = Math.max(0, Math.min(SOUND_VOL_MAX, v)); });
  loadInt(HOURLY_CHIME_KEY, v => { STATE.cfgHourlyChime = v !== 0; });
  loadInt(CLAUDE_DING_KEY, v => { STATE.cfgClaudeDing = v !== 0; });
  loadInt(SOUND_ALERTS_KEY, v => { STATE.cfgSoundAlerts = v !== 0; });
  loadInt(MUTE_SLEEP_KEY, v => { STATE.cfgMuteSleep = v !== 0; });
  loadInt(MUTE_NIGHT_KEY, v => { STATE.cfgMuteNight = v !== 0; });
  loadInt(OTA_AUTO_KEY, v => { STATE.cfgOtaAuto = v !== 0; });
  loadInt(BATTERY_SAVE_KEY, v => { STATE.cfgBatterySaveMode = Math.max(0, Math.min(2, v)); });
  loadInt(REDUCE_MOTION_KEY, v => { STATE.cfgReduceMotion = v !== 0; });
  loadInt(HIGH_CONTRAST_KEY, v => { STATE.cfgHighContrast = v !== 0; });
  loadInt(LIGHT_MODE_KEY, v => { STATE.cfgLightMode = v !== 0; });
  applyThemeTokens();  // includes applyContrast()
}
