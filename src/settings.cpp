// Settings sheet (tap the status strip's gear). A generic two-screen system:
// SET_LIST is a scrolling list of rows (design.md 11.7), SET_LEAF is a detail
// screen (11.15) with an option grid (11.8) or an arm button (11.9). Every
// setting is one SettingDef row -- a kind, a small set of {value,label}
// options, and two plain function pointers -- so adding a setting is a data
// row + a short apply()/getCurrent() pair, not a hand-copied page.
//
// Gestures and transitions (scroll momentum, rubber band, push/pop, swipe
// back, drag to dismiss) live in nav.cpp; this file draws, hit-tests and
// commits. Carried over from the CYD firmware, re-designed per design.md.
#include "state.h"

// Increase Contrast's runtime tokens (tokens.h), set by applyContrast().
uint16_t TOK_COLOR_TEXT_SECONDARY = TOK_GRAY_5;
uint16_t TOK_COLOR_FILL_TRACK = TOK_GRAY_3;
bool TOK_CARD_OUTLINE = false;

char toastText[40] = "";
uint32_t toastUntilMs = 0;

enum RowKind : uint8_t {
  ROW_NAV,     // current value + chevron; pushes a detail with an option grid
  ROW_TOGGLE,  // labelled On/Off pill; flips in place, no detail screen
  ROW_ACTION,  // status.error label; pushes a detail with the arm button
};

struct SettingDef {
  const char* label;      // row label and detail title (sentence case)
  const char* subtitle;   // detail subtitle; "" to omit
  const char* hint;       // informative hint under the grid, or "" (never a "Tap to...")
  RowKind kind;
  uint8_t cols, count;    // first grid row's columns (the rest go on row 2); option count
  const int* values;           // raw option values, length == count
  const char* const* valueLabels;  // cell text, length == count
  int (*getCurrent)();         // value to select (and show on the row)
  void (*apply)(int value);    // live mutation + queues flash persistence
  const char* doneToast;       // action rows: the toast shown when it executes
};

static const int BRIGHTNESS_VALUES[5] = {0, 64, 128, 191, 255};
static const char* const BRIGHTNESS_LABELS[5] = {"0%", "25%", "50%", "75%", "100%"};

// Config keys persisted through the generic queue (see pendingConfigSave in
// state.h and sd_store.cpp's saveIntConfigToFlash()/main.cpp's
// networkTask()). Index here must match the id passed to queueConfigSave()
// from each setting's apply(). These are also the NVS key names used in
// flash (Preferences), which caps key length at 15 chars. The old names are
// kept where a setting was only relabelled (show_countdown = Pace bars,
// hourly_flash = Hourly signal, show_progress = Poll progress).
const char* const CONFIG_KEY_NAMES[CFGKEY_COUNT] = {
  "brightness", "poll_sec", "pixel_shift_min", "boot_page", "cat_shuffle_sec",
  "night_mode", "screen_rotation", "show_countdown", "battery_save", "show_aqi",
  "hourly_flash", "show_progress", "last_page", "reduce_motion", "high_contrast"
};

void queueConfigSave(uint8_t keyId, int32_t value) {
  pendingConfigKeyId = keyId;
  pendingConfigValue = value;
  pendingConfigSave = true;
}

// Live backlight from user preference + night-mode overlay only, faded by the
// given motion.backlight.* duration (0 = cut). Battery Save never touches
// brightness. Screen sleep pins it at 0 so a night-mode edge (or any other
// caller) can't light a sleeping panel.
void applyEffectiveBrightness(uint32_t fadeMs) {
  if (screenSleeping) {
    backlightFadeTo(0, 0);
    return;
  }
  backlightFadeTo(nightDimActive ? NIGHT_MODE_DIM_VALUE : (uint8_t)cfgBrightness, fadeMs);
}

// Live poll cadence: user's Poll Interval, floored to 2 min while Battery
// Save is active (Settings ON, or AUTO + Mac power.battery_save) or the
// screen is asleep. networkTask only reads POLL_INTERVAL_MS.
void applyEffectivePoll() {
  uint32_t sec = cfgPollIntervalSec;
  if (sec < 5) sec = 5;
  if (sec > 3600) sec = 3600;
  if ((batterySaveActive() || screenSleeping) && sec < BATTERY_SAVE_POLL_SEC) sec = BATTERY_SAVE_POLL_SEC;
  POLL_INTERVAL_MS = sec * 1000;
}

// Increase Contrast (design.md 12.9): text.secondary -> 0xBDF7, fill.track ->
// gray.4, cards gain a 1px gray.4 outline. Meaning unchanged, separation up.
void applyContrast() {
  TOK_COLOR_TEXT_SECONDARY = cfgHighContrast ? TOK_GRAY_5_HC : TOK_GRAY_5;
  TOK_COLOR_FILL_TRACK = cfgHighContrast ? TOK_GRAY_4 : TOK_GRAY_3;
  TOK_CARD_OUTLINE = cfgHighContrast;
}

static int getCurrentBrightness() { return cfgBrightness; }
static void applyBrightness(int v) {
  cfgBrightness = (uint8_t)v;
  applyEffectiveBrightness(TOK_MOTION_BACKLIGHT_ADJUST_MS);  // may stay dimmed under night mode
  queueConfigSave(CFGKEY_BRIGHTNESS, v);
}

// Action rows (Restart, Forget Wi-Fi) have no current value to select.
static int getCurrentNone() { return -1; }
static const int ACTION_VALUES[1] = {0};
// Arms the restart rather than calling ESP.restart() directly here on core 1
// -- that could cut power to core 0 mid-SD-write; networkTask drains
// pendingRestart between its own sequential SD operations instead.
static void applyRestart(int v) { pendingRestart = true; }
static const char* const RESTART_LABELS[1] = {"Restart"};
// Erasing the flash-saved WiFi creds + the restart-into-AP-portal must both
// happen on networkTask (core 0) -- see forgetWifiFromFlash()/pendingForgetWifi
// -- so apply() here just arms the flag and returns immediately.
static void applyForgetWifi(int v) { pendingForgetWifi = true; }
static const char* const FORGET_WIFI_LABELS[1] = {"Forget Wi-Fi"};

// Poll interval: seconds (matches the "poll_sec" NVS key directly). The row
// shows the *user* preference, not the Battery-Save-stretched effective rate.
static const int POLL_VALUES[5] = {5, 10, 20, 60, 300};
static const char* const POLL_LABELS[5] = {"5s", "10s", "20s", "60s", "5m"};
static int getCurrentPollInterval() { return (int)cfgPollIntervalSec; }
static void applyPollInterval(int v) {
  cfgPollIntervalSec = (uint32_t)v;
  applyEffectivePoll();  // networkTask reads the (possibly floored) POLL_INTERVAL_MS
  queueConfigSave(CFGKEY_POLL_INTERVAL, v);
}

// Anti-retention pixel shift: cfgShiftStepMs is read only by pixelShiftTick()
// on core 1, so it's mutated directly.
static const int PIXEL_SHIFT_VALUES[4] = {0, 1, 3, 10};
static const char* const PIXEL_SHIFT_LABELS[4] = {"Off", "1m", "3m", "10m"};
static int getCurrentPixelShift() { return (int)(cfgShiftStepMs / 60000); }
static void applyPixelShift(int v) {
  cfgShiftStepMs = (uint32_t)v * 60000;
  queueConfigSave(CFGKEY_PIXEL_SHIFT, v);
}

// Boot page: which page currentPage starts on next boot, in carousel order.
// Auto (BOOT_PAGE_AUTO) resumes cfgLastPage. Device Stats isn't here -- it's
// a sheet, not a page.
static const int PAGE_VALUES[8] = {BOOT_PAGE_AUTO, 0, 1, 2, 3, 4, 5, 6};
static const char* const PAGE_LABELS[8] = {
  "Auto", "Status", "Projects", "Limits", "Cats", "Movies", "Status + cats", "Note"
};
static int getCurrentBootPage() { return cfgBootPage; }
static void applyBootPage(int v) {
  cfgBootPage = v;
  if (v != BOOT_PAGE_AUTO) currentPage = v;  // jump the live dashboard too -- a free,
                                              // immediate effect; Auto isn't itself a page
  queueConfigSave(CFGKEY_BOOT_PAGE, v);
}

// Cat shuffle: how long each cat plays before rotating. Fixed (-1) disables
// auto-rotation; the shuffle media control is then the only way to change it.
static const int CAT_SHUFFLE_VALUES[5] = {-1, 0, 5, 10, 30};
static const char* const CAT_SHUFFLE_LABELS[5] = {"Fixed", "Off", "5s", "10s", "30s"};
static int getCurrentCatShuffle() { return catShuffleFixed ? -1 : (int)(catShuffleMs / 1000); }
static void applyCatShuffle(int v) {
  catShuffleFixed = (v < 0);
  catShuffleMs = catShuffleFixed ? 0 : (uint32_t)v * 1000;
  queueConfigSave(CFGKEY_CAT_SHUFFLE, v);
}

// Battery Save: stretches short usage polls to 2 min only (no backlight
// change). Off / On / Auto -- Auto follows Mac /api/usage power.battery_save.
static const int BATTERY_SAVE_VALUES[3] = {BATTERY_SAVE_OFF, BATTERY_SAVE_ON, BATTERY_SAVE_AUTO};
static const char* const BATTERY_SAVE_LABELS[3] = {"Off", "On", "Auto"};
static int getCurrentBatterySave() { return cfgBatterySaveMode; }
static void applyBatterySave(int v) {
  cfgBatterySaveMode = constrain(v, BATTERY_SAVE_OFF, BATTERY_SAVE_AUTO);
  applyEffectivePoll();
  queueConfigSave(CFGKEY_BATTERY_SAVE, cfgBatterySaveMode);
}

// Rotation flip: 1 = normal, 3 = 180 degrees (the CYD's LovyanGFX rotation
// numbers). On the S3 it maps to display.cpp's software rotate and
// touch_axs.cpp's inverse map -- live, next frame, nothing to recalibrate.
static const int ROTATION_VALUES[2] = {1, 3};
static const char* const ROTATION_LABELS[2] = {"Normal", "Flipped"};
static int getCurrentRotation() { return cfgScreenRotation; }
static void applyRotation(int v) {
  cfgScreenRotation = v;
  displaySetFlipped(cfgScreenRotation == 3);
  queueConfigSave(CFGKEY_ROTATION, v);
}

// Toggle rows: getCurrent() is 0/1, apply() gets the new state.
static const int ONOFF_VALUES[2] = {0, 1};
static const char* const ONOFF_LABELS[2] = {"Off", "On"};

static int getCurrentNightMode() { return cfgNightModeOn ? 1 : 0; }
static void applyNightMode(int v) {
  cfgNightModeOn = (v != 0);
  if (!cfgNightModeOn && nightDimActive) {
    nightDimActive = false;
    applyEffectiveBrightness(TOK_MOTION_BACKLIGHT_NIGHT_MS);  // restore user brightness
  }
  queueConfigSave(CFGKEY_NIGHT_MODE, v);
}
static int getCurrentShowCountdown() { return cfgShowCountdown ? 1 : 0; }
static void applyShowCountdown(int v) { cfgShowCountdown = (v != 0); queueConfigSave(CFGKEY_SHOW_COUNTDOWN, v); }
static int getCurrentShowAqi() { return cfgShowAqi ? 1 : 0; }
static void applyShowAqi(int v) { cfgShowAqi = (v != 0); queueConfigSave(CFGKEY_SHOW_AQI, v); }
static int getCurrentHourlyFlash() { return cfgHourlyFlash ? 1 : 0; }
static void applyHourlyFlash(int v) { cfgHourlyFlash = (v != 0); queueConfigSave(CFGKEY_HOURLY_FLASH, v); }
static int getCurrentShowProgress() { return cfgShowProgress ? 1 : 0; }
static void applyShowProgress(int v) { cfgShowProgress = (v != 0); queueConfigSave(CFGKEY_SHOW_PROGRESS, v); }
static int getCurrentReduceMotion() { return cfgReduceMotion ? 1 : 0; }
static void applyReduceMotion(int v) { cfgReduceMotion = (v != 0); queueConfigSave(CFGKEY_REDUCE_MOTION, v); }
static int getCurrentHighContrast() { return cfgHighContrast ? 1 : 0; }
static void applyHighContrast(int v) {
  cfgHighContrast = (v != 0);
  applyContrast();
  queueConfigSave(CFGKEY_HIGH_CONTRAST, v);
}

// Ordered by frequency of use (design.md 13.3): display, then content, then
// system, then accessibility, and the destructive actions last.
static const SettingDef SETTINGS[] = {
  { "Brightness", "Backlight brightness", "", ROW_NAV, 5, 5,
    BRIGHTNESS_VALUES, BRIGHTNESS_LABELS, getCurrentBrightness, applyBrightness, nullptr },
  { "Night mode", "", "", ROW_TOGGLE, 2, 2,
    ONOFF_VALUES, ONOFF_LABELS, getCurrentNightMode, applyNightMode, nullptr },
  { "Rotation", "For upside-down mounting", "", ROW_NAV, 2, 2,
    ROTATION_VALUES, ROTATION_LABELS, getCurrentRotation, applyRotation, nullptr },
  { "Boot page", "Page shown after a restart", "Auto resumes the last page shown", ROW_NAV, 4, 8,
    PAGE_VALUES, PAGE_LABELS, getCurrentBootPage, applyBootPage, nullptr },
  { "Cat shuffle", "How long each cat plays", "Off plays each cat to its end; Fixed keeps one", ROW_NAV, 5, 5,
    CAT_SHUFFLE_VALUES, CAT_SHUFFLE_LABELS, getCurrentCatShuffle, applyCatShuffle, nullptr },
  { "Pace bars", "", "", ROW_TOGGLE, 2, 2,
    ONOFF_VALUES, ONOFF_LABELS, getCurrentShowCountdown, applyShowCountdown, nullptr },
  { "Show AQI", "", "", ROW_TOGGLE, 2, 2,
    ONOFF_VALUES, ONOFF_LABELS, getCurrentShowAqi, applyShowAqi, nullptr },
  { "Hourly signal", "", "", ROW_TOGGLE, 2, 2,
    ONOFF_VALUES, ONOFF_LABELS, getCurrentHourlyFlash, applyHourlyFlash, nullptr },
  { "Poll progress", "", "", ROW_TOGGLE, 2, 2,
    ONOFF_VALUES, ONOFF_LABELS, getCurrentShowProgress, applyShowProgress, nullptr },
  { "Poll interval", "How often to fetch /api/usage", "", ROW_NAV, 5, 5,
    POLL_VALUES, POLL_LABELS, getCurrentPollInterval, applyPollInterval, nullptr },
  { "Battery save", "Polls at most every 2 min while on", "Auto follows the Mac's power state", ROW_NAV, 3, 3,
    BATTERY_SAVE_VALUES, BATTERY_SAVE_LABELS, getCurrentBatterySave, applyBatterySave, nullptr },
  { "Pixel shift", "Anti-retention orbit interval", "", ROW_NAV, 4, 4,
    PIXEL_SHIFT_VALUES, PIXEL_SHIFT_LABELS, getCurrentPixelShift, applyPixelShift, nullptr },
  { "Reduce motion", "", "", ROW_TOGGLE, 2, 2,
    ONOFF_VALUES, ONOFF_LABELS, getCurrentReduceMotion, applyReduceMotion, nullptr },
  { "Increase contrast", "", "", ROW_TOGGLE, 2, 2,
    ONOFF_VALUES, ONOFF_LABELS, getCurrentHighContrast, applyHighContrast, nullptr },
  { "Forget Wi-Fi", "Erase the saved network and reboot into setup", "Tap twice to erase Wi-Fi", ROW_ACTION, 1, 1,
    ACTION_VALUES, FORGET_WIFI_LABELS, getCurrentNone, applyForgetWifi, "Wi-Fi forgotten, restarting..." },
  { "Restart", "Reboot the board", "Tap twice to restart the board", ROW_ACTION, 1, 1,
    ACTION_VALUES, RESTART_LABELS, getCurrentNone, applyRestart, "Restarting..." },
};
static const int SETTINGS_COUNT = sizeof(SETTINGS) / sizeof(SETTINGS[0]);

// ── LIST GEOMETRY (design.md 11.7 / 11.14) ─────────────────
// Viewport y 44..319 under the modal header; the first row rests at y 52.
static const int LIST_X = TOK_LAYOUT_CONTENT_X0, LIST_W = TOK_LAYOUT_CONTENT_W;
static const int LIST_VIEW_Y0 = TOK_LAYOUT_HEADER_H;
static const int LIST_ROW0_Y = TOK_LAYOUT_HEADER_CONTENT_Y;
static const int LIST_STEP = TOK_LIST_ROW_H + TOK_SPACE_LIST_GAP;   // 64
static const int LIST_TRAIL_R = LIST_X + LIST_W - TOK_LIST_ROW_INSET;  // 456 (exclusive right of the trailing element)

int settingsScrollMax() {
  int contentBottom = LIST_ROW0_Y + SETTINGS_COUNT * LIST_STEP;  // last row + the bottom margin
  return max(0, contentBottom - SCREEN_H);
}

static int rowY(int idx) { return LIST_ROW0_Y + idx * LIST_STEP - settingsScrollOffset; }

// Nav rows show their current value: the selected preset's label, or the raw
// value when flash holds something that isn't a preset.
static String currentValueText(const SettingDef& d) {
  int cur = d.getCurrent();
  for (int i = 0; i < d.count; i++)
    if (d.values[i] == cur) return String(d.valueLabels[i]);
  if (d.values == BRIGHTNESS_VALUES) return String((cur * 100 + 127) / 255) + "%";
  if (d.values == POLL_VALUES) return String(cur) + "s";
  return String("--");
}

// Toggle pill (design.md 11.7): 52 x 28, radius.full; On = accent + "On" in
// text.onAccent, Off = fill.track + "Off" in secondary -- the label names the
// state, so colour is never the only cue.
static void drawTogglePill(int xRight, int cy, bool on) {
  int x = xRight - TOK_TOGGLE_W, y = cy - TOK_TOGGLE_H / 2;
  aaFillRoundRect(x, y, TOK_TOGGLE_W, TOK_TOGGLE_H, TOK_TOGGLE_H / 2,
                         on ? TOK_COLOR_ACCENT : TOK_COLOR_FILL_TRACK);
  drawTextC(TOK_TYPE_HEADLINE, x + TOK_TOGGLE_W / 2, cy - fontLineH(TOK_TYPE_HEADLINE) / 2, on ? "On" : "Off",
            on ? TOK_COLOR_TEXT_ON_ACCENT : TOK_COLOR_TEXT_SECONDARY);
}

// Scroll-edge shade (design.md 8.4): once scrolled, the 8 rows passing under
// the header (y 44..51) are drawn at material.scrim, so the list visibly
// slides under it -- never a divider line. The sprite holds big-endian
// RGB565, hence the swaps around the shift-and-mask.
static void shadeRows(int y0, int y1) {
  uint16_t* fb = (uint16_t*)frame.getBuffer();
  for (int y = y0; y < y1; y++) {
    uint16_t* p = fb + y * SCREEN_W;
    for (int x = 0; x < SCREEN_W; x++) {
      uint16_t c = (uint16_t)((p[x] >> 8) | (p[x] << 8));
      c = (c >> 1) & 0x7BEF;
      p[x] = (uint16_t)((c >> 8) | (c << 8));
    }
  }
}

static void drawSettingsList() {
  g->fillScreen(TOK_COLOR_BG_CANVAS);
  g->setClipRect(0, LIST_VIEW_Y0, SCREEN_W, SCREEN_H - LIST_VIEW_Y0);
  for (int idx = 0; idx < SETTINGS_COUNT; idx++) {
    const SettingDef& d = SETTINGS[idx];
    int y = rowY(idx);
    if (y + TOK_LIST_ROW_H < LIST_VIEW_Y0 || y >= SCREEN_H) continue;
    bool pressed = (pressedId == PRESS_ROW && pressedIndex == idx);
    drawCardSurface(LIST_X, y, LIST_W, TOK_LIST_ROW_H, pressed ? TOK_COLOR_SURFACE_RAISED : TOK_COLOR_SURFACE_CARD);
    int cy = y + TOK_LIST_ROW_H / 2;
    drawText(TOK_TYPE_HEADLINE, LIST_X + TOK_LIST_ROW_INSET, cy - fontLineH(TOK_TYPE_HEADLINE) / 2, d.label,
             d.kind == ROW_ACTION ? TOK_COLOR_STATUS_ERROR : TOK_COLOR_TEXT_PRIMARY);
    if (d.kind == ROW_TOGGLE) {
      drawTogglePill(LIST_TRAIL_R, cy, d.getCurrent() != 0);
    } else {
      int chevCx = LIST_TRAIL_R - 4;
      drawChevron(chevCx, cy, TOK_COLOR_TEXT_TERTIARY);
      if (d.kind == ROW_NAV)
        drawTextR(TOK_TYPE_BODY, chevCx - 2 - TOK_SPACE_SM, cy - fontLineH(TOK_TYPE_BODY) / 2,
                  currentValueText(d), TOK_COLOR_TEXT_SECONDARY);
    }
  }
  g->clearClipRect();
  if (settingsScrollOffset > 0) shadeRows(LIST_VIEW_Y0, LIST_ROW0_Y);

  // Scroll thumb: text.tertiary, radius.full, in the rows' right padding.
  int maxS = settingsScrollMax();
  if (maxS > 0) {
    const int trackY = LIST_ROW0_Y, trackH = TOK_LAYOUT_OVERLAY_CONTENT_Y1 - LIST_ROW0_Y;
    const int contentH = SETTINGS_COUNT * LIST_STEP;
    int thumbH = max(24, trackH * trackH / contentH);
    int off = constrain(settingsScrollOffset, 0, maxS);
    int thumbY = trackY + (trackH - thumbH) * off / maxS;
    aaFillRoundRect(LIST_X + LIST_W - 6, thumbY, 4, thumbH, 2, TOK_COLOR_TEXT_TERTIARY);
  }
  drawModalHeader(false, "Settings", pressedId == PRESS_CLOSE);
}

// ── DETAIL GEOMETRY (design.md 11.15 / 11.8) ───────────────
static const int CELL_W_BY_COLS[6] = {0, 464, 228, 149, 110, 86};

static int gridTop(const SettingDef& d) {
  int y = TOK_LAYOUT_HEADER_CONTENT_Y;
  if (d.subtitle[0]) y += fontLineH(TOK_TYPE_BODY) + TOK_SPACE_XL;
  return y;
}

// Cell i's rect. Row 1 holds `cols` cells, row 2 the rest, each row sized by
// the column table and centred on the content width.
static void cellRect(const SettingDef& d, int i, int& x, int& y, int& w, int& h) {
  int row = i < d.cols ? 0 : 1;
  int n = row == 0 ? min((int)d.cols, (int)d.count) : d.count - d.cols;
  int k = row == 0 ? i : i - d.cols;
  w = CELL_W_BY_COLS[n];
  h = d.kind == ROW_ACTION ? TOK_BUTTON_LG : TOK_OPTION_CELL_H;
  int total = n * w + (n - 1) * TOK_SPACE_SM;
  x = TOK_LAYOUT_CONTENT_X0 + (TOK_LAYOUT_CONTENT_W - total) / 2 + k * (w + TOK_SPACE_SM);
  y = gridTop(d) + row * (TOK_OPTION_CELL_H + TOK_SPACE_SM);
}

static bool armedNow(uint32_t now) {
  return confirmArmedRow == settingsLeafIndex && now - confirmArmedMs < CONFIRM_ARM_MS;
}

static void drawToast() {
  if (!toastText[0] || (int32_t)(millis() - toastUntilMs) >= 0) return;
  const int padX = TOK_SPACE_MD;
  int w = textW(TOK_TYPE_BODY, toastText) + 2 * padX;
  int x = (SCREEN_W - w) / 2, y = TOK_LAYOUT_OVERLAY_CONTENT_Y1 - TOK_TOAST_H;
  aaFillRoundRect(x, y, w, TOK_TOAST_H, TOK_RADIUS_SM, TOK_COLOR_SURFACE_RAISED);
  drawText(TOK_TYPE_BODY, x + padX, y + (TOK_TOAST_H - fontLineH(TOK_TYPE_BODY)) / 2, toastText,
           TOK_COLOR_TEXT_PRIMARY);
}

static void drawSettingsLeaf() {
  const SettingDef& d = SETTINGS[settingsLeafIndex];
  g->fillScreen(TOK_COLOR_BG_CANVAS);
  drawModalHeader(true, d.label, pressedId == PRESS_CLOSE);
  if (d.subtitle[0])
    drawText(TOK_TYPE_BODY, TOK_LAYOUT_CONTENT_X0, TOK_LAYOUT_HEADER_CONTENT_Y, d.subtitle, TOK_COLOR_TEXT_SECONDARY);

  // A value that isn't an exact preset selects nothing. Action rows are
  // armed / unarmed instead (confirmArmedRow).
  int current = d.kind == ROW_ACTION ? INT_MIN : d.getCurrent();
  bool armed = d.kind == ROW_ACTION && armedNow(millis());
  int bottom = 0;
  for (int i = 0; i < d.count; i++) {
    int x, y, w, h;
    cellRect(d, i, x, y, w, h);
    bool pressed = (pressedId == PRESS_CELL && pressedIndex == i);
    uint16_t fill, ink;
    const char* label = d.valueLabels[i];
    if (d.kind == ROW_ACTION) {
      fill = armed ? TOK_COLOR_STATUS_ERROR : (pressed ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_SURFACE_CARD);
      ink = armed ? TOK_COLOR_TEXT_ON_ACCENT : TOK_COLOR_STATUS_ERROR;
      if (armed) label = "Tap again";
    } else {
      bool sel = d.values[i] == current;
      fill = sel ? (pressed ? TOK_COLOR_ACCENT_PRESSED : TOK_COLOR_ACCENT)
                 : (pressed ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_SURFACE_CARD);
      ink = sel ? TOK_COLOR_TEXT_ON_ACCENT : TOK_COLOR_TEXT_PRIMARY;
    }
    drawCardSurface(x, y, w, h, fill);
    drawTextC(TOK_TYPE_HEADLINE, x + w / 2, y + (h - fontLineH(TOK_TYPE_HEADLINE)) / 2, label, ink);
    bottom = y + h;
  }
  if (d.hint[0])
    drawTextC(TOK_TYPE_CAPTION, SCREEN_W / 2, bottom + TOK_SPACE_LG, d.hint, TOK_COLOR_TEXT_SECONDARY);
  drawToast();
}

void drawSettingsScreen() {
  if (settingsScreen == SET_LEAF) drawSettingsLeaf();
  else drawSettingsList();
  drawSystemCorner(false);
}

void renderSettings() {
  // Neither screen touches STATE, so no stateMutex needed here.
  drawSettingsScreen();
  presentFrame();
}

// ── HIT TESTING / COMMIT ───────────────────────────────────
int settingsListHit(int32_t x, int32_t y) {
  if (y < LIST_VIEW_Y0 || x < LIST_X || x >= LIST_X + LIST_W) return -1;
  for (int idx = 0; idx < SETTINGS_COUNT; idx++) {
    int ry = rowY(idx);
    if (y >= ry && y < ry + TOK_LIST_ROW_H) return idx;
  }
  return -1;
}

bool settingsListActivate(int idx) {
  if (idx < 0 || idx >= SETTINGS_COUNT) return false;
  const SettingDef& d = SETTINGS[idx];
  if (d.kind == ROW_TOGGLE) {
    d.apply(d.getCurrent() ? 0 : 1);  // flips in place on the commit present
    return false;
  }
  settingsLeafIndex = idx;
  confirmArmedRow = -1;
  return true;
}

int settingsLeafHit(int32_t x, int32_t y) {
  if (settingsLeafIndex < 0) return -1;
  const SettingDef& d = SETTINGS[settingsLeafIndex];
  for (int i = 0; i < d.count; i++) {
    int cx, cy, w, h;
    cellRect(d, i, cx, cy, w, h);
    if (x >= cx && x < cx + w && y >= cy && y < cy + h) return i;
  }
  return -1;
}

// Commit on up: option cells apply immediately and persist silently; the
// arm button needs two taps within touch.armWindow (the only confirmation).
void settingsLeafActivate(int i, uint32_t now) {
  const SettingDef& d = SETTINGS[settingsLeafIndex];
  if (d.kind == ROW_ACTION) {
    if (armedNow(now)) {
      confirmArmedRow = -1;
      snprintf(toastText, sizeof(toastText), "%s", d.doneToast);
      toastUntilMs = now + TOK_MOTION_TOAST_MS;
      renderSettings();       // the toast lands before networkTask restarts the board
      d.apply(d.values[i]);
      return;
    }
    confirmArmedRow = settingsLeafIndex;
    confirmArmedMs = now;
    return;
  }
  d.apply(d.values[i]);
}
