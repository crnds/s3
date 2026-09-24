// Frame presentation (pixel shift, page slide, hourly flash) + every
// full-screen page's draw function, re-laid out for the S3's 480x320 panel
// with anti-aliased fonts (fonts.h). This is the near-line-for-line twin of
// simulator-s3.html's page-drawing functions -- see CLAUDE.md's
// firmware/simulator parity rule before touching layout, colours, or text
// here without updating the simulator too.
//
// Ported from ~/cyd's pages.cpp: same pages, same information, same colour
// semantics and rules (pace flag, AQI colours, note tokenizer); coordinates
// and type are new.
#include "state.h"

// Shared by status-page digital date and weather overlay daily rows.
static const char* const WDAY_ABBR[7] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char* const MON_ABBR[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// ── PRESENTATION ───────────────────────────────────────────
bool presentHold = false;
volatile uint32_t teWaitAccumUs = 0;

void presentFrame() {
  if (presentHold) return;
  bool isEvenSecond = false;
  displaySetInvert(checkHourlyFlash(isEvenSecond) && isEvenSecond);
  displayPresent((const uint16_t*)frame.getBuffer(), shiftX, shiftY, COL_BG);
  teWaitAccumUs += displayLastTeWaitUs();
  shiftDirty = false;
}

// Outgoing frame for the page slide. PSRAM, allocated on first use.
static uint16_t* prevFrame = nullptr;
static const uint32_t SLIDE_MS = 180;

void pageTransitionBegin() {
  if (!prevFrame)
    prevFrame = (uint16_t*)heap_caps_malloc(SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
  if (prevFrame) memcpy(prevFrame, frame.getBuffer(), SCREEN_W * SCREEN_H * 2);
  presentHold = true;
}

// Ease-out cubic slide from prevFrame to `frame`. Each step is one full
// present (~17-33ms, TE-locked), so the slide is ~6-10 frames; progress is
// driven by wall time, not frame count, so it takes SLIDE_MS regardless.
void pageTransitionRun(bool forward) {
  presentHold = false;
  if (!prevFrame) { presentFrame(); return; }
  bool isEvenSecond = false;
  displaySetInvert(checkHourlyFlash(isEvenSecond) && isEvenSecond);
  uint32_t t0 = millis();
  for (;;) {
    uint32_t el = millis() - t0;
    if (el >= SLIDE_MS) break;
    float t = (float)el / SLIDE_MS;
    float e = 1.0f - (1.0f - t) * (1.0f - t) * (1.0f - t);
    displayPresentSlide(prevFrame, (const uint16_t*)frame.getBuffer(), (int)(e * SCREEN_W),
                        forward, shiftX, shiftY, COL_BG);
    teWaitAccumUs += displayLastTeWaitUs();
  }
  presentFrame();
}

// Tap feedback for the cat pages' middle "next cat" band (CAT_ADVANCE_X0/X1):
// a thin white perimeter for one ~60ms flash. It's a present-time overlay
// (display.cpp's border), so the frame itself is never dirtied.
void flashTouchCenter() {
  displaySetBorder(5, 0xFFFF);
  presentFrame();
  delay(60);
  displaySetBorder(0, 0);
  presentFrame();
}

// Advance the pixel-shift orbit (core 1 only, like everything display-side).
// Returns true on a step so the caller can repaint immediately.
bool pixelShiftTick(uint32_t now) {
  if (cfgShiftStepMs == 0) return false;
  if (now - lastShiftMs < cfgShiftStepMs) return false;
  lastShiftMs = now;
  shiftIdx = (uint8_t)((shiftIdx + 1) % 8);
  shiftX = SHIFT_ORBIT[shiftIdx][0];
  shiftY = SHIFT_ORBIT[shiftIdx][1];
  shiftDirty = true;
  return true;
}

bool checkHourlyFlash(bool& isEvenSecond) {
  if (!cfgHourlyFlash) return false;  // Settings > HOURLY FLASH off: no signal at all
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 0)) {
    if (timeinfo.tm_min == 0 && timeinfo.tm_sec < 6) {
      isEvenSecond = (timeinfo.tm_sec % 2 == 0);
      return true;
    }
  }
  return false;
}

// ── FOOTER ─────────────────────────────────────────────────
static const int FOOTER_TEXT_Y = 297;   // SM line box 297..313, baseline 310
static const int FOOTER_CY = 305;       // icon centre line
static const int PROGRESS_Y = SCREEN_H - 1;

// Small 3-bar signal icon next to the pulse dot: green when the most recent
// poll found WiFi up (wifiOk), rose when it didn't. Separate from the pulse
// dot so a WiFi outage and a server outage (WiFi fine, Mac unreachable) read
// as two distinct signals.
static void drawWifiIcon() {
  uint16_t c = wifiOk ? COL_GOOD : COL_WARN;
  g->fillRect(30, 307, 3, 4, c);
  g->fillRect(35, 303, 3, 8, c);
  g->fillRect(40, 299, 3, 12, c);
}

// Footer settings gear, bottom-right corner (SETTINGS_HIT_*): hub disc with a
// punched-out centre + 8 short spokes, grey so it reads as UI chrome.
static void drawSettingsIcon() {
  const int cx = 452, cy = FOOTER_CY;
  g->fillCircle(cx, cy, 5, COL_TEXT2);
  g->fillCircle(cx, cy, 2, COL_BG);
  g->drawWideLine(cx, cy - 9, cx, cy - 6, 1.5f, COL_TEXT2);
  g->drawWideLine(cx, cy + 6, cx, cy + 9, 1.5f, COL_TEXT2);
  g->drawWideLine(cx - 9, cy, cx - 6, cy, 1.5f, COL_TEXT2);
  g->drawWideLine(cx + 6, cy, cx + 9, cy, 1.5f, COL_TEXT2);
  g->drawWideLine(cx - 6, cy - 6, cx - 4, cy - 4, 1.5f, COL_TEXT2);
  g->drawWideLine(cx + 4, cy + 4, cx + 6, cy + 6, 1.5f, COL_TEXT2);
  g->drawWideLine(cx - 6, cy + 6, cx - 4, cy + 4, 1.5f, COL_TEXT2);
  g->drawWideLine(cx + 4, cy - 4, cx + 6, cy - 6, 1.5f, COL_TEXT2);
}

static int progressLineW() {
  uint32_t elapsed = millis() - lastPollMs;
  if (elapsed > POLL_INTERVAL_MS) elapsed = POLL_INTERVAL_MS;
  return (int)((float)elapsed / POLL_INTERVAL_MS * SCREEN_W);
}
static int progressDrawnW = 0;  // how much of the line is currently in `frame`

void drawFooter() {
  // Status dot: server reachability only, gated on wifiOk (WiFi down is the
  // wifi icon's job). Blinks green once a second while polls reach the
  // server; static amber when WiFi is fine but the Mac isn't answering.
  bool onPhase = (millis() / 1000) % 2;
  if (wifiOk) {
    if (!connected) g->fillCircle(18, FOOTER_CY, 4, COL_ACCENT);
    else if (onPhase) g->fillCircle(18, FOOTER_CY, 5, COL_GOOD);
  }
  drawWifiIcon();
  drawSettingsIcon();

  String pageStr = String(currentPage + 1) + " / " + String(PAGE_COUNT);
  drawTextR(FONT_SM, SETTINGS_HIT_X0 - 8, FOOTER_TEXT_Y, pageStr, COL_TEXT2);

  uint32_t flashUsed, flashTotal, ramUsed, ramTotal;
  int romPct = flashPercent(flashUsed, flashTotal);
  int ramPct = staticRamPercent(ramUsed, ramTotal);
  int cpuInt = (int)(cpuPercentAvg + 0.5f);

  int x = 52;
  x = drawText(FONT_SM, x, FOOTER_TEXT_Y, "CPU ", COL_TEXT2);
  x = drawText(FONT_SM, x, FOOTER_TEXT_Y, String(cpuInt) + "%", COL_TEXT);
  x = drawText(FONT_SM, x, FOOTER_TEXT_Y, "   ROM ", COL_TEXT2);
  x = drawText(FONT_SM, x, FOOTER_TEXT_Y, String(romPct) + "%", COL_TEXT);
  x = drawText(FONT_SM, x, FOOTER_TEXT_Y, "   RAM ", COL_TEXT2);
  drawText(FONT_SM, x, FOOTER_TEXT_Y, String(ramPct) + "%", COL_TEXT);

  // 1px line along the bottom edge, filling left-to-right as the next poll
  // approaches (full width = fetch imminent). progressTick() extends it
  // between renders so it glides instead of stepping at 1Hz.
  progressDrawnW = 0;
  if (!cfgShowProgress) return;
  int w = progressLineW();
  if (w > 0) g->fillRect(0, PROGRESS_Y, w, 1, COL_TEXT2);
  progressDrawnW = w;
}

// Between-render top-up of the progress line, straight into `frame`. Only on
// pages that own a footer (loop() gates it). Returns true if pixels changed.
bool progressTick(uint32_t nowMs) {
  if (!cfgShowProgress || !connected) return false;
  int w = progressLineW();
  if (w < progressDrawnW) {  // new poll cycle: clear and restart
    g->fillRect(0, PROGRESS_Y, SCREEN_W, 1, COL_BG);
    progressDrawnW = 0;
  }
  if (w <= progressDrawnW) return false;
  g->fillRect(progressDrawnW, PROGRESS_Y, w - progressDrawnW, 1, COL_TEXT2);
  progressDrawnW = w;
  return true;
}

// Small top-right corner overlay shown on every page whenever Battery Save is
// *active* (Settings ON, or AUTO + Mac power.battery_save). No-ops when
// inactive. A solid backing box keeps it legible over cat frames.
void drawBatterySaveIcon() {
  if (!batterySaveActive()) return;
  const int boxW = SCREEN_W - 3 - BATTERY_ICON_X0, boxH = BATTERY_ICON_Y1 - BATTERY_ICON_Y0 + 1;
  g->fillRect(BATTERY_ICON_X0, BATTERY_ICON_Y0, boxW, boxH, COL_BG);
  const int bodyX = 450, bodyY = 7, bodyW = 19, bodyH = 11;
  const int nubW = 3, nubH = 5;
  g->fillRoundRect(bodyX, bodyY, bodyW, bodyH, 2, COL_YELLOW);
  g->fillRect(bodyX + bodyW, bodyY + (bodyH - nubH) / 2, nubW, nubH, COL_YELLOW);
}

// ── DRAWING HELPERS ────────────────────────────────────────
// Percent track+fill. minFillPx is the smallest non-zero fill. Returns fill
// width or -1 when percent is unknown.
static int drawPercentBar(int x, int y, int w, int h, int percent,
                          uint16_t color, uint16_t trackColor = COL_TRACK,
                          int minFillPx = 3) {
  g->fillRect(x, y, w, h, trackColor);
  if (percent < 0) return -1;
  int fillW = (int)((float)min(percent, 100) / 100 * w + 0.5f);
  if (fillW < minFillPx) fillW = minFillPx;
  g->fillRect(x, y, fillW, h, color);
  return fillW;
}

// Live countdown: tick the last server-provided remaining-seconds down by
// wall time since lastFetchOkMs.
static long liveResetsInSec(long baseSec) {
  if (baseSec < 0) return -1;
  long rem = baseSec - (long)((millis() - STATE.lastFetchOkMs) / 1000);
  return rem < 0 ? 0 : rem;
}

// One row of the /usage-style limits page: label left, right-aligned value,
// thin bar under. percent < 0 leaves the track empty (unknown).
static void drawLimitsRow(int y, const String& label, const String& right, int percent) {
  drawText(FONT_MD, 15, y, label, COL_TEXT);
  drawTextR(FONT_MD, 465, y, right, COL_TEXT2);
  drawPercentBar(15, y + 27, 450, 10, percent, COL_ACCENT);
}

// "Resets Jul 16, 04:59  58%" -- or bare "58%", or "--" when unknown.
static String limitRowText(int percent, const char* resets) {
  if (percent < 0) return "--";
  String s;
  if (resets[0] != '\0') s = String("Resets ") + resets + "  ";
  s += String(percent) + "%";
  return s;
}

// Limits page (index 2): Claude Code /usage panel -- context window, 5-hour
// limit, weekly (all models), weekly per-model (hidden when the server sends
// null), usage credits. Rows shift up when a row is absent.
static void drawLimitsPage() {
  drawText(FONT_SMB, 15, 8, "USAGE LIMITS", COL_TEXT);

  int y = 34;
  const int STEP = 51;
  String ctx = STATE.ctxTokens >= 0
      ? fmtTokens(STATE.ctxTokens) + "  " + String(STATE.ctxPercent) + "%"
      : String("--");
  drawLimitsRow(y, "Context window", ctx, STATE.ctxPercent); y += STEP;

  drawLimitsRow(y, "5-hour limit",
                limitRowText(STATE.sessionPercent, STATE.sessionResets),
                STATE.sessionPercent); y += STEP;

  drawLimitsRow(y, "Weekly (all models)",
                limitRowText(STATE.weekPercent, STATE.weekResets),
                STATE.weekPercent); y += STEP;

  if (STATE.weekModelPercent >= 0) {
    String name = STATE.weekModelName[0] != '\0' ? String(STATE.weekModelName) : String("model");
    drawLimitsRow(y, "Weekly (" + name + ")",
                  limitRowText(STATE.weekModelPercent, STATE.weekModelResets),
                  STATE.weekModelPercent); y += STEP;
  }

  if (STATE.creditsUsed >= 0) {
    drawLimitsRow(y, "Usage credits",
                  fmtCost(STATE.creditsUsed) + " of " + fmtCost(STATE.creditsLimit),
                  STATE.creditsPercent);
  }
}

// Page 1: top 4 projects (7d) in the upper half, the 7-day token trend chart
// in the lower half, split by a 1px divider.
static void drawProjectsPage() {
  // ── upper half: top projects (7d) ──
  drawText(FONT_SMB, 15, 8, "TOP PROJECTS (7D)", COL_TEXT);

  if (STATE.projectCount == 0) {
    drawText(FONT_SM, 15, 34, "No data yet", COL_TEXT2);
  } else {
    int shown = STATE.projectCount < 4 ? STATE.projectCount : 4;
    int64_t maxTokens = 1;
    for (int i = 0; i < shown; i++) {
      if (STATE.projectTokens[i] > maxTokens) maxTokens = STATE.projectTokens[i];
    }

    int y = 32;
    const int barMaxW = 360;
    for (int i = 0; i < shown; i++) {
      drawText(FONT_SM, 15, y, STATE.projectNames[i], COL_TEXT);

      int barW = (int)((float)STATE.projectTokens[i] / maxTokens * barMaxW);
      g->fillRect(15, y + 19, barMaxW, 10, COL_SURFACE);
      g->fillRoundRect(15, y + 19, max(barW, 5), 10, 2, COL_ACCENT);

      drawText(FONT_SM, 15 + barMaxW + 12, y + 15, fmtTokens(STATE.projectTokens[i]), COL_TEXT2);
      y += 35;
    }
  }

  // ── lower half: 7-day trend ──
  g->fillRect(15, 176, 462, 1, COL_BORDER);
  drawText(FONT_SMB, 15, 184, "7-DAY TREND", COL_TEXT);

  int64_t maxTrend = 1;
  for (int i = 0; i < 7; i++) {
    if (STATE.trend[i] > maxTrend) maxTrend = STATE.trend[i];
  }

  const char* dayLabels[7] = {"-6", "-5", "-4", "-3", "-2", "-1", "today"};
  const int chartX = 36, chartY = 208, chartH = 62, barW = 51, gap = 12;
  for (int i = 0; i < 7; i++) {
    int barH = (int)((float)STATE.trend[i] / maxTrend * chartH);
    int bx = chartX + i * (barW + gap);
    int by = chartY + chartH - barH;
    g->fillRoundRect(bx, by, barW, max(barH, 3), 4, COL_ACCENT);
    drawTextC(FONT_SM, bx + barW / 2, chartY + chartH + 3, dayLabels[i], COL_TEXT2);
  }
}

// Simple vector weather icon (no bitmap glyphs), mapped from Open-Meteo's WMO
// weather_code, CENTRED on (cx, cy) and scaled by k (1.0 = the CYD's ~18px
// icon). Every offset below is the CYD's, times k.
static void drawWeatherIcon(int cx, int cy, int code, float k) {
  auto S = [k](float v) { return (int)lroundf(v * k); };
  float lw = 1.0f * k;
  if (code < 0) {
    drawTextC(FONT_SM, cx, cy - fontLineH(FONT_SM) / 2, "--", COL_TEXT2);
    return;
  }
  if (code == 0 || code == 1) {
    // clear: sun disc + 8 short rounded rays with a gap between disc and rays.
    g->fillCircle(cx, cy, S(4), COL_YELLOW);
    g->drawWideLine(cx, cy - S(9), cx, cy - S(7), lw, COL_YELLOW);
    g->drawWideLine(cx, cy + S(7), cx, cy + S(9), lw, COL_YELLOW);
    g->drawWideLine(cx - S(9), cy, cx - S(7), cy, lw, COL_YELLOW);
    g->drawWideLine(cx + S(7), cy, cx + S(9), cy, lw, COL_YELLOW);
    g->drawWideLine(cx - S(7), cy - S(7), cx - S(5), cy - S(5), lw, COL_YELLOW);
    g->drawWideLine(cx + S(5), cy + S(5), cx + S(7), cy + S(7), lw, COL_YELLOW);
    g->drawWideLine(cx - S(7), cy + S(7), cx - S(5), cy + S(5), lw, COL_YELLOW);
    g->drawWideLine(cx + S(5), cy - S(5), cx + S(7), cy - S(7), lw, COL_YELLOW);
    return;
  }
  // Rain/snow/lightning are drawn standalone (no cloud underneath) so the
  // condition itself reads clearly at small sizes.
  if (code >= 95) {
    // thunderstorm: zigzag bolt as 4 triangles. Vertices:
    // A(-1,+8) B(-1,+2) C(-5,+2) D(+1,-8) E(+1,-2) F(+5,-2).
    g->fillTriangle(cx - S(5), cy + S(2), cx + S(1), cy - S(8), cx + S(1), cy - S(2), COL_YELLOW);
    g->fillTriangle(cx - S(5), cy + S(2), cx + S(1), cy - S(2), cx - S(1), cy + S(2), COL_YELLOW);
    g->fillTriangle(cx - S(1), cy + S(2), cx + S(1), cy - S(2), cx + S(5), cy - S(2), COL_YELLOW);
    g->fillTriangle(cx - S(1), cy + S(2), cx + S(5), cy - S(2), cx - S(1), cy + S(8), COL_YELLOW);
    return;
  }
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    // snow: six-armed snowflake = three rounded lines crossing at 60deg
    g->drawWideLine(cx, cy - S(7), cx, cy + S(7), lw, COL_TEXT);
    g->drawWideLine(cx - S(6), cy - S(4), cx + S(6), cy + S(4), lw, COL_TEXT);
    g->drawWideLine(cx - S(6), cy + S(4), cx + S(6), cy - S(4), lw, COL_TEXT);
    return;
  }
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    // rain: three staggered teardrops, two small on top, one large below.
    g->fillTriangle(cx - S(6), cy - S(8), cx - S(8), cy - S(3), cx - S(4), cy - S(3), COL_BLUE);
    g->fillCircle(cx - S(6), cy - S(3), S(2), COL_BLUE);
    g->fillTriangle(cx + S(5), cy - S(6), cx + S(3), cy - S(1), cx + S(7), cy - S(1), COL_BLUE);
    g->fillCircle(cx + S(5), cy - S(1), S(2), COL_BLUE);
    g->fillTriangle(cx - S(1), cy + S(1), cx - S(4), cy + S(6), cx + S(2), cy + S(6), COL_BLUE);
    g->fillCircle(cx - S(1), cy + S(6), S(3), COL_BLUE);
    return;
  }
  // Everything else shares a plain cloud (2/3/45/48 = cloudy/fog, or any
  // unmapped code): two overlapping puffs on a fully-rounded pill base.
  g->fillCircle(cx - S(4), cy - S(2), S(4), COL_TEXT2);
  g->fillCircle(cx + S(3), cy - S(3), S(5), COL_TEXT2);
  g->fillRoundRect(cx - S(9), cy - S(2), S(19), S(9), S(4), COL_TEXT2);
}

// Thin inset progress bar (min fill = bar height so a stub is full-height).
// Returns fill width (-1 when unknown) for shine-sweep overlays.
static int drawMiniBar(int x, int y, int w, int percent, uint16_t color,
                       uint16_t trackColor = COL_TRACK, int h = 8) {
  return drawPercentBar(x, y, w, h, percent, color, trackColor, h);
}

// ── SHINE SWEEP ────────────────────────────────────────────
// Looping light band swept across the green reset-countdown bars' fill in
// drawLimitsCard (status page + MIXED_PAGE + NOTE_PAGE). The band crosses the
// full track at constant speed but is only painted over the filled interior.
// Twin of simulator-s3.html's drawShineStrip.
static const uint32_t SHINE_PERIOD_MS = 2600;
static const int SHINE_BAND_R = 10;
static const int SHINE_BAR_X = 16, SHINE_BAR_W = 208, SHINE_BAR_H = 6;
static const int SHINE_BAR_Y[2] = {70, 195};  // 5h, weekly (drawLimitsCard)
// Fill widths cached by drawLimitsCard (under stateMutex) so shineTick can
// repaint between renders without touching STATE.
static volatile int shineFillPx[2] = {-1, -1};
// Only columns whose colour changed since the last paint of the same bar are
// rewritten -- the band moves a few px per pass.
static int shinePrevCenter[2] = {INT_MIN, INT_MIN};

static inline uint16_t shineColor(int i, int center) {
  int d = abs(i - center);
  return d <= 2 ? COL_SHINE_HI : d <= 6 ? COL_SHINE_MID : d <= SHINE_BAND_R ? COL_SHINE_LO : COL_GOOD;
}

// Paint the band over one bar's filled interior (columns 2..fillW-3, so the
// ends stay untouched). Returns true if any column changed.
static bool drawShineStrip(int x, int y, int fillW, uint32_t nowMs, int barIdx, bool force) {
  if (fillW < 14) return false;
  float phase = (float)(nowMs % SHINE_PERIOD_MS) / SHINE_PERIOD_MS;
  int center = -SHINE_BAND_R + (int)(phase * (SHINE_BAR_W + 2 * SHINE_BAND_R));
  int prev = force ? INT_MIN : shinePrevCenter[barIdx];
  if (prev == center) return false;
  shinePrevCenter[barIdx] = center;
  bool changed = false;
  for (int i = 2; i <= fillW - 3; i++) {
    uint16_t c = shineColor(i, center);
    if (prev != INT_MIN && c == shineColor(i, prev)) continue;
    g->fillRect(x + i, y, 1, SHINE_BAR_H, c);
    changed = true;
  }
  return changed;
}

// Between-render top-up of the shine band, straight into `frame`: no lock
// (only the volatile cached fill widths). Returns true if pixels changed.
bool shineTick(uint32_t nowMs) {
  // NOTE_PAGE and MIXED_PAGE show the same left column as the status page.
  if (currentPage != 0 && currentPage != MIXED_PAGE && currentPage != NOTE_PAGE) return false;
  bool changed = false;
  for (int i = 0; i < 2; i++) {
    if (drawShineStrip(SHINE_BAR_X, SHINE_BAR_Y[i], shineFillPx[i], nowMs, i, false)) changed = true;
  }
  return changed;
}

// Uppercase section label.
static void drawCardLabel(int x, int y, const char* label) {
  drawText(FONT_SMB, x, y, label, COL_TEXT2);
}

static void drawCard(int x, int y, int w, int h) {
  g->fillRect(x, y, w, h, COL_SURFACE);
  g->drawRect(x, y, w, h, COL_BORDER);
}

static const long SESSION_WINDOW_SEC = 5L * 3600;       // 5h
static const long WEEK_WINDOW_SEC = 7L * 24 * 3600;     // 168h

// Reset-countdown fill: 0% right after a reset, 100% right before the next.
static int elapsedPercentOfWindow(long remainingSec, long windowSec) {
  if (remainingSec < 0) return -1;
  long elapsed = windowSec - remainingSec;
  if (elapsed < 0) elapsed = 0;
  int pct = (int)(elapsed * 100 / windowSec);
  return constrain(pct, 0, 100);
}

// Projected time until 100% "if this pace continues" -- the average rate
// since the window's own start (currentPct / elapsedSec). "" when unknown or
// the reset would land first ("the reset wins, say nothing").
static String formatPaceDur(int currentPct, long elapsedSec, long remainingSec) {
  if (currentPct <= 0 || elapsedSec <= 0 || remainingSec < 0) return "";
  float remainingPct = 100 - currentPct;
  if (remainingPct <= 0) return "";
  float ratePerSec = (float)currentPct / (float)elapsedSec;
  float secToExhaust = remainingPct / ratePerSec;
  if (secToExhaust >= remainingSec) return "";  // reset wins, say nothing
  if (secToExhaust < 3600.0f) {
    int m = (int)(secToExhaust / 60.0f + 0.5f);
    return String(m < 1 ? 1 : m) + "m";
  }
  return String((int)(secToExhaust / 3600.0f + 0.5f)) + "h";
}

// A warning is only ever earned by pace, never by level -- callers gate
// `ahead` on actual > pace + deadband. `baseline` is the big percent's
// baseline; the flag sits on it.
static void drawPaceFlag(int x, int baseline, bool ahead, int currentPct, long elapsedSec, long remainingSec) {
  if (!ahead) return;
  int x2 = drawText(FONT_MDB, x, baseline - fontAscent(FONT_MDB), "!", COL_WARN);
  String dur = formatPaceDur(currentPct, elapsedSec, remainingSec);
  if (dur.length() > 0) drawText(FONT_MD, x2 + 3, baseline - fontAscent(FONT_MD), dur, COL_TEXT2);
}

// One limits card half: big percent + label (+ pace flag), orange usage bar,
// green reset-countdown bar with its shine, and the two reset lines.
static void drawLimitHalf(int topY, const char* label, int percent, bool ahead, long elapsed, long rem,
                          int pace, int barIdx, const char* resets, const String& inText,
                          FontId resetsFont) {
  String pctStr = percent >= 0 ? String(percent) + "%" : "--";
  const int pctY = topY + 9;
  const int baseline = pctY + fontAscent(FONT_LG);
  int x = drawText(FONT_LG, 16, pctY, pctStr, COL_ACCENT);
  drawCardLabel(x + 8, baseline - fontAscent(FONT_SMB), label);
  drawPaceFlag(x + 8 + textW(FONT_SMB, label) + 8, baseline, ahead, percent, elapsed, rem);

  const int barY = SHINE_BAR_Y[barIdx] - 14;
  drawMiniBar(16, barY, SHINE_BAR_W, percent, COL_ACCENT);
  // Green reset-countdown bars (and their shine) are optional via Settings
  // "Show Countdown"; when off, clear the cached fill so shineTick no-ops.
  if (cfgShowCountdown) {
    shineFillPx[barIdx] = drawMiniBar(16, SHINE_BAR_Y[barIdx], SHINE_BAR_W, pace, COL_GOOD, COL_TRACK_BLACK, SHINE_BAR_H);
    drawShineStrip(SHINE_BAR_X, SHINE_BAR_Y[barIdx], shineFillPx[barIdx], millis(), barIdx, true);
  } else {
    shineFillPx[barIdx] = -1;
  }

  const int resetsY = SHINE_BAR_Y[barIdx] + 12;
  drawText(resetsFont, 16, resetsY,
           resets[0] != '\0' ? String("resets ") + resets : String("resets --"), COL_TEXT2);
  if (inText.length()) drawText(FONT_MD, 16, resetsY + 23, inText, COL_TEXT);
}

// Left column of the status / mixed / note pages: two cards (5h, week).
static void drawLimitsCard() {
  drawCard(LEFT_X, 3, LEFT_W, 132);
  drawCard(LEFT_X, 137, LEFT_W, 116);

  long sessionRem = liveResetsInSec(STATE.sessionResetsInSec);
  long weekRem = liveResetsInSec(STATE.weekResetsInSec);

  // QUOTA PACING: "% of the window elapsed" (also the green countdown bar)
  // vs actual usage -- a flag is earned only by running ahead of pace.
  int sessionPace = elapsedPercentOfWindow(sessionRem, SESSION_WINDOW_SEC);
  int weekPace = elapsedPercentOfWindow(weekRem, WEEK_WINDOW_SEC);
  bool sessionAhead = STATE.sessionPercent >= 0 && sessionPace >= 0 &&
                       STATE.sessionPercent > sessionPace + 2;  // 2pt deadband stops boundary flicker
  bool weekAhead = STATE.weekPercent >= 0 && weekPace >= 0 &&
                    STATE.weekPercent > weekPace + 2;
  long sessionElapsed = sessionRem >= 0 ? SESSION_WINDOW_SEC - sessionRem : -1;
  long weekElapsed = weekRem >= 0 ? WEEK_WINDOW_SEC - weekRem : -1;

  drawLimitHalf(3, "5H", STATE.sessionPercent, sessionAhead, sessionElapsed, sessionRem,
                sessionPace, 0, STATE.sessionResets,
                sessionRem >= 0 ? "in " + fmtCountdown(sessionRem) : String(""), FONT_MD);
  drawLimitHalf(137, "WK", STATE.weekPercent, weekAhead, weekElapsed, weekRem,
                weekPace, 1, STATE.weekResets,
                weekRem >= 0 ? "in " + fmtCountdownDHM(weekRem) : String(""), FONT_MD);
}

// BTC price card under the week card: in-line "BTCUSDT <price>".
static void drawBtcCard() {
  drawCard(LEFT_X, 255, LEFT_W, 35);
  const int baseline = 279;
  int x = drawText(FONT_SM, 16, baseline - fontAscent(FONT_SM), "BTCUSDT", COL_TEXT2);
  drawText(FONT_MDB, x + 10, baseline - fontAscent(FONT_MDB), fmtBtc(STATE.btcPrice), COL_TEXT);
}

// ── NOTE PAGE (NOTE_PAGE) ──────────────────────────────────
// Right-hand pane, same box as the status page's right column.
static const int NOTE_X = RIGHT_X;
static const int NOTE_Y = 3;
static const int NOTE_W = RIGHT_W;
static const int NOTE_H = CONTENT_Y1 - 3;
static const int NOTE_TX = NOTE_X + 9;  // 250, first glyph column
// First text row sits below BATTERY_ICON_Y1: drawBatterySaveIcon() punches a
// COL_BG box into the top-right corner on *every* page after the content, so
// text starting higher would lose the end of its first line whenever Battery
// Save is on. The band that buys holds the "NOTE" label.
static const int NOTE_TY = 26;
// Exclusive bottom limit: a row is drawn only while y + lineH <= this.
static const int NOTE_TY_MAX = NOTE_Y + NOTE_H - 5;  // 285
// Usable width 470 - 250 = 220px. Monospace advance per size (make_vlw.py):
//   size | font  | adv | step | cols | rows
//     1  | MONO1 |  7  |  15  |  31  |  17
//     2  | MONO2 |  10 |  21  |  22  |  12
//     3  | MONO3 |  13 |  29  |  16  |   8
// Every size holds at least the CYD pane's columns (24/12/8), so a note
// written against the CYD's note.html fit check wraps no worse here.
static const int NOTE_TEXT_W = 220;
static const FontId NOTE_FONTS[3] = {FONT_MONO1, FONT_MONO2, FONT_MONO3};

// ── Syntax highlighting ───────────────────────────────────────────────────
// This rule set is duplicated in simulator-s3.html, and in ~/cyd's pages.cpp,
// simulator.html, note.html and note.py; all must agree or the board and the
// editor disagree about what the text looks like. Tokenizing happens at two
// levels -- source line, then word -- plus a one-character backtick toggle.
// There is deliberately no per-character classification.
//
// Precedence, first match wins:
//   1. inside a `backtick span`      -> COL_BLUE (delimiters included)
//   2. word is TODO/FIXME/BUG        -> COL_WARN
//      word is DONE/OK               -> COL_GOOD
//      word is numeric               -> COL_YELLOW
//   3. line begins '#'               -> COL_ACCENT (whole line)
//      line begins '>'               -> COL_TEXT2  (whole line)
//   4. leading "- ", "* ", "+ "      -> COL_ACCENT (the marker char only)
//   5. otherwise                     -> COL_TEXT
//
// Keywords are matched case-SENSITIVE uppercase-only.
static bool notePunctOpen(char c) {
  return c == '(' || c == '[' || c == '{' || c == '"' || c == '\'';
}

static bool notePunctClose(char c) {
  return c == ')' || c == ']' || c == '}' || c == ':' || c == ';' ||
         c == ',' || c == '.' || c == '!' || c == '?' || c == '"' || c == '\'';
}

static bool noteIsDigit(char c) { return c >= '0' && c <= '9'; }

// Characters allowed inside a numeric token alongside the digits, so
// "12:30", "$1,200", "2026-08-26" and "50%" each colour as one unit.
static bool noteIsNumChar(char c) {
  return noteIsDigit(c) || c == '.' || c == ',' || c == ':' || c == '/' ||
         c == '%' || c == '$' || c == '+' || c == '-';
}

static bool noteRangeEquals(const char* s, int a, int b, const char* w) {
  int n = strlen(w);
  return (b - a) == n && strncmp(s + a, w, n) == 0;
}

// Word-level colour for s[a..b), or 0 for "no override". Wrapping punctuation
// is trimmed for the comparison only.
static uint16_t noteWordColor(const char* s, int a, int b) {
  while (a < b && notePunctOpen(s[a])) a++;
  while (b > a && notePunctClose(s[b - 1])) b--;
  if (b == a) return 0;
  if (noteRangeEquals(s, a, b, "TODO") || noteRangeEquals(s, a, b, "FIXME") ||
      noteRangeEquals(s, a, b, "BUG")) return COL_WARN;
  if (noteRangeEquals(s, a, b, "DONE") || noteRangeEquals(s, a, b, "OK")) return COL_GOOD;
  bool hasDigit = false;
  for (int i = a; i < b; i++) {
    if (!noteIsNumChar(s[i])) return 0;
    if (noteIsDigit(s[i])) hasDigit = true;
  }
  return hasDigit ? COL_YELLOW : 0;
}

// Word-wrapped, syntax-coloured render of STATE.note into the right-hand pane.
// Overflow past NOTE_TY_MAX is simply not drawn ("wrap + clip").
// Caller holds stateMutex; this must not re-lock.
static void drawNotePane() {
  drawCard(NOTE_X, NOTE_Y, NOTE_W, NOTE_H);
  drawCardLabel(NOTE_TX, 7, "NOTE");

  const char* s = STATE.note;
  const int size = constrain(STATE.noteSize, 1, 3);
  const FontId font = NOTE_FONTS[size - 1];
  const int glyphW = FONT_ADV[font]['0' - 0x20];  // monospace: every advance is equal
  const int glyphH = fontLineH(font);
  const int lineH = glyphH;
  const int cols = NOTE_TEXT_W / glyphW;

  if (s[0] == '\0') {
    const int cx = NOTE_X + NOTE_W / 2;
    drawTextC(FONT_SM, cx, 132, "no note yet", COL_TEXT2);
    drawTextC(FONT_SM, cx, 152, "edit at :8787/note", COL_TEXT2);
    return;
  }

  int y = NOTE_TY;
  int col = 0;
  const int n = strlen(s);
  int i = 0;

  while (i < n) {
    // ── extent of this source line ──
    int lineEnd = i;
    while (lineEnd < n && s[lineEnd] != '\n') lineEnd++;

    // ── line-level context, decided once ──
    int p = i;
    while (p < lineEnd && s[p] == ' ') p++;  // allow indented markers
    uint16_t lineColor = 0;
    int markerIdx = -1;
    if (p < lineEnd) {
      if (s[p] == '#') lineColor = COL_ACCENT;
      else if (s[p] == '>') lineColor = COL_TEXT2;
      else if ((s[p] == '-' || s[p] == '*' || s[p] == '+') &&
               (p + 1 == lineEnd || s[p + 1] == ' ')) markerIdx = p;
    }
    bool inCode = false;  // an unmatched backtick stains only its own line
    col = 0;

    // ── emit the source line as one or more visual rows ──
    while (i < lineEnd) {
      if (s[i] == ' ') {
        // Leading spaces on a wrapped row collapse; a source line's own
        // indentation collapses too.
        if (col > 0) col++;
        i++;
        if (col >= cols) {
          y += lineH; col = 0;
          if (y + glyphH > NOTE_TY_MAX) return;
        }
        continue;
      }

      // Word lookahead: [i, wEnd) is the next run of non-space characters.
      int wEnd = i;
      while (wEnd < lineEnd && s[wEnd] != ' ') wEnd++;
      const int wLen = wEnd - i;

      // Wrap before a word that doesn't fit here but would fit on a fresh
      // row. A word longer than a whole row is hard-broken below instead.
      if (col > 0 && col + wLen > cols && wLen <= cols) {
        y += lineH; col = 0;
        if (y + glyphH > NOTE_TY_MAX) return;
        continue;  // re-test on the new row
      }

      const uint16_t wordColor = noteWordColor(s, i, wEnd);

      while (i < wEnd) {
        if (col >= cols) {  // hard break inside an over-long word
          y += lineH; col = 0;
          if (y + glyphH > NOTE_TY_MAX) return;
        }
        uint16_t c;
        if (s[i] == '`') {
          inCode = !inCode;
          c = COL_BLUE;  // colour the tick itself, so a stray one is visible
        } else if (inCode) {
          c = COL_BLUE;
        } else if (wordColor) {
          c = wordColor;
        } else if (lineColor) {
          c = lineColor;
        } else if (i == markerIdx) {
          c = COL_ACCENT;
        } else {
          c = COL_TEXT;
        }
        drawChar(font, NOTE_TX + col * glyphW, y, s[i], c);
        col++; i++;
      }
    }

    // ── end of source line (an empty line consumes a row, as an editor implies) ──
    i = lineEnd + 1;  // step over the '\n'
    y += lineH; col = 0;
    if (y + glyphH > NOTE_TY_MAX) return;
  }
}

// Page 6: the mixed page's left column, with the note pane where the cats go.
static void drawNotePage() {
  drawLimitsCard();
  drawBtcCard();
  drawNotePane();
}

// Mixed page's static half: left column + footer. The right pane belongs to
// the GIF player (gif_player.cpp draws the 2x-downscaled cat there).
void drawMixedPageStatic() {
  g->fillRect(0, 0, MIXED_GIF_X0, SCREEN_H, COL_BG);
  g->fillRect(MIXED_GIF_X0, CONTENT_Y1, SCREEN_W - MIXED_GIF_X0, SCREEN_H - CONTENT_Y1, COL_BG);
  drawLimitsCard();
  drawBtcCard();
  drawFooter();
}

// Timer region fill colour: COL_GOOD blended 50% into COL_BG (no alpha on the
// panel), precomputed per RGB565 channel -> 0x1B65 (~rgb(25,109,41)).
const uint16_t COL_GOOD_50 = 0x1B65;

// Filled pie wedge from the hour hand clockwise to the reset angle.
static void drawTimerWedge(int cx, int cy, int r, float startAngle, float endAngle) {
  float delta = fmodf(endAngle - startAngle, 360.0f);
  if (delta < 0) delta += 360.0f;
  g->fillArc(cx, cy, r, 0, startAngle, startAngle + delta, COL_GOOD_50);
}

// Minimal analog clock: circle, 12 ticks, hour/minute/second hands, the
// green 5h-reset radius and (optionally) the timer wedge. Angles are
// screen-space with -90deg so 0 points up.
static void drawAnalogClock(int cx, int cy, int r, int hour24, int minute, int second,
                             bool haveReset, int resetHour24, int resetMinute) {
  float hourAngle = ((hour24 % 12) + minute / 60.0f) * 30.0f - 90.0f;
  float minAngle = (minute + second / 60.0f) * 6.0f - 90.0f;
  float secAngle = second * 6.0f - 90.0f;
  float hourRad = hourAngle * PI / 180.0f;
  float minRad = minAngle * PI / 180.0f;
  float secRad = secAngle * PI / 180.0f;

  if (haveReset && cfgShowCountdown) {
    float resetAngleWedge = ((resetHour24 % 12) + resetMinute / 60.0f) * 30.0f - 90.0f;
    drawTimerWedge(cx, cy, r - 1, hourAngle, resetAngleWedge);
  }

  g->drawCircle(cx, cy, r, COL_TEXT);
  g->drawCircle(cx, cy, r - 1, COL_TEXT);
  for (int i = 0; i < 12; i++) {
    float tickRad = (i * 30.0f - 90.0f) * PI / 180.0f;
    int x0 = cx + (int)(cosf(tickRad) * (r - 2));
    int y0 = cy + (int)(sinf(tickRad) * (r - 2));
    int x1 = cx + (int)(cosf(tickRad) * (r - 8));
    int y1 = cy + (int)(sinf(tickRad) * (r - 8));
    g->drawWideLine(x0, y0, x1, y1, 1.0f, COL_TEXT);
  }

  int hx = cx + (int)(cosf(hourRad) * r * 0.5f);
  int hy = cy + (int)(sinf(hourRad) * r * 0.5f);
  int mx = cx + (int)(cosf(minRad) * r * 0.8f);
  int my = cy + (int)(sinf(minRad) * r * 0.8f);
  int sx = cx + (int)(cosf(secRad) * (r - 3));
  int sy = cy + (int)(sinf(secRad) * (r - 3));

  g->drawWideLine(cx, cy, hx, hy, 3.0f, COL_TEXT);
  g->drawWideLine(cx, cy, mx, my, 2.0f, COL_TEXT);

  // Session (5h) reset time: a thin green radius, drawn before the second
  // hand so the sweeping hand stays on top.
  if (haveReset) {
    float resetAngle = ((resetHour24 % 12) + resetMinute / 60.0f) * 30.0f - 90.0f;
    float resetRad = resetAngle * PI / 180.0f;
    int rx = cx + (int)(cosf(resetRad) * (r - 3));
    int ry = cy + (int)(sinf(resetRad) * (r - 3));
    g->drawWideLine(cx, cy, rx, ry, 0.8f, COL_GOOD);
  }

  g->drawWideLine(cx, cy, sx, sy, 1.2f, COL_ACCENT);
  g->fillCircle(cx, cy, 3, COL_TEXT);
}

// AQI badge colours, per the US EPA / aqicn.org scale; fg picked per box by
// WCAG contrast (black wins everywhere except Hazardous's dark maroon).
// Mirrored exactly in simulator-s3.html's aqiColors().
static void aqiColors(int aqi, uint16_t& bg, uint16_t& fg) {
  if (aqi <= 50)       { bg = COL_GOOD;       fg = COL_TRACK_BLACK; } // Good
  else if (aqi <= 100) { bg = COL_YELLOW;     fg = COL_TRACK_BLACK; } // Moderate
  else if (aqi <= 150) { bg = COL_AQI_ORANGE; fg = COL_TRACK_BLACK; } // Unhealthy for Sensitive Groups
  else if (aqi <= 200) { bg = COL_WARN;       fg = COL_TRACK_BLACK; } // Unhealthy
  else if (aqi <= 300) { bg = COL_PURPLE;     fg = COL_TRACK_BLACK; } // Very Unhealthy
  else                 { bg = COL_MAROON;     fg = COL_TEXT; }        // Hazardous
}

// Rounded AQI badge with its number, top-left at (x, y). Returns its width.
static const int AQI_BADGE_H = 24;
static int aqiBadgeW(int aqi) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", aqi);
  return textW(FONT_MDB, buf) + 12;
}
static void drawAqiBadge(int x, int y, int aqi) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", aqi);
  uint16_t bg, fg;
  aqiColors(aqi, bg, fg);
  g->fillRoundRect(x, y, aqiBadgeW(aqi), AQI_BADGE_H, 5, bg);
  drawText(FONT_MDB, x + 6, y + (AQI_BADGE_H - fontLineH(FONT_MDB)) / 2, buf, fg);
}

// Status page (page 0). Left column: 5h / week limits cards + BTC card.
// Right column: Bangkok analog + digital clock and date (with the AQI
// badge), then the weather card (now, today's H/L, next 3 hours) -- tap it
// for the Weather overlay.
static void drawStatusPage() {
  drawLimitsCard();
  drawBtcCard();
  drawCard(RIGHT_X, 3, RIGHT_W, 216);
  drawCard(RIGHT_X, 221, RIGHT_W, 69);

  struct tm timeinfo;
  bool haveTime = getLocalTime(&timeinfo, 0);

  long sessionRem = liveResetsInSec(STATE.sessionResetsInSec);
  bool haveReset = haveTime && sessionRem >= 0;
  int resetHour = 0, resetMinute = 0;
  if (haveReset) {
    long totalSec = ((long)timeinfo.tm_hour * 3600 + timeinfo.tm_min * 60 + timeinfo.tm_sec + sessionRem) % 86400;
    resetHour = totalSec / 3600;
    resetMinute = (totalSec % 3600) / 60;
  }

  // Row 1: analog clock, centred on the card.
  const int clockCx = RIGHT_X + RIGHT_W / 2, clockCy = 80, clockR = 68;
  if (haveTime) {
    drawAnalogClock(clockCx, clockCy, clockR, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                     haveReset, resetHour, resetMinute);
  } else {
    g->drawCircle(clockCx, clockCy, clockR, COL_TEXT);
  }

  // Row 2: digital time + date, centred under the clock.
  if (haveTime) {
    char hm[8];
    snprintf(hm, sizeof(hm), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    drawTextC(FONT_LG, clockCx, 152, hm, COL_TEXT);
  } else {
    drawTextC(FONT_LG, clockCx, 152, "--:--", COL_TEXT2);
  }

  if (haveTime) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%s %d %s", WDAY_ABBR[timeinfo.tm_wday], timeinfo.tm_mday,
             MON_ABBR[timeinfo.tm_mon]);
    int dateW = textW(FONT_MD, buf);
    // AQI badge appended after the date, e.g. "Mon 3 Aug [81]"; skipped when
    // the Mac hasn't delivered a reading (STATE.aqi < 0).
    bool haveAqi = cfgShowAqi && STATE.aqi >= 0;
    int badgeW = haveAqi ? aqiBadgeW(STATE.aqi) : 0;
    int gap = haveAqi ? 8 : 0;
    int startX = clockCx - (dateW + gap + badgeW) / 2;
    const int dateY = 190;
    drawText(FONT_MD, startX, dateY, buf, COL_TEXT2);
    if (haveAqi) drawAqiBadge(startX + dateW + gap, dateY - 1, STATE.aqi);
  } else {
    drawTextC(FONT_MD, clockCx, 190, "--", COL_TEXT2);
  }

  // ── weather card (y 221..289) ──
  // 1px divider between the "now" block and the next-3-hours forecast.
  g->fillRect(323, 229, 1, 53, COL_BORDER);

  // Today's high / low, stacked bright-over-grey at the card's left edge.
  drawText(FONT_SM, 250, 231, STATE.weatherHigh > -900 ? String(STATE.weatherHigh) : String("--"), COL_TEXT);
  drawText(FONT_SM, 250, 263, STATE.weatherLow > -900 ? String(STATE.weatherLow) : String("--"), COL_TEXT2);

  // "Now": icon over temp, centred between the H/L column and the divider.
  const int NOW_CENTER_X = 295;
  drawWeatherIcon(NOW_CENTER_X, 241, STATE.weatherCode, 1.4f);
  if (STATE.weatherTempC > -900) {
    String tempStr = String((int)round(STATE.weatherTempC));
    int tw = textW(FONT_MDB, tempStr) + textW(FONT_SM, "C");
    int x = NOW_CENTER_X - tw / 2;
    const int baseline = 279;
    x = drawText(FONT_MDB, x, baseline - fontAscent(FONT_MDB), tempStr, COL_TEXT);
    drawText(FONT_SM, x, baseline - fontAscent(FONT_SM), "C", COL_TEXT2);
  } else {
    drawTextC(FONT_MDB, NOW_CENTER_X, 261, "--", COL_TEXT);
  }

  // Next 3 hours: weatherHourly[] starts at the current hour (index 0, the
  // "now" block), so indices 1..3. Columns: hour label, icon, temp.
  const int SLOT_X = 324, SLOT_W = 51;
  for (int i = 0; i < 3; i++) {
    int idx = i + 1;
    int cx = SLOT_X + i * SLOT_W + SLOT_W / 2;
    bool have = STATE.weatherHourlyCount > idx;

    char hbuf[4];
    if (have) snprintf(hbuf, sizeof(hbuf), "%02d", STATE.weatherHourly[idx].hour);
    else snprintf(hbuf, sizeof(hbuf), "--");
    drawTextC(FONT_SM, cx, 225, hbuf, COL_TEXT2);

    drawWeatherIcon(cx, 254, have ? STATE.weatherHourly[idx].code : -1, 1.2f);

    char tbuf[6];
    if (have) snprintf(tbuf, sizeof(tbuf), "%dC", STATE.weatherHourly[idx].tempC);
    else snprintf(tbuf, sizeof(tbuf), "--");
    drawTextC(FONT_SM, cx, 268, tbuf, COL_TEXT);
  }
}

// One Device Stats block: title, bar + percent, subtitle.
static void drawFullStatBlock(int y, const char* title, int percent, const String& sub, uint16_t color) {
  drawText(FONT_SMB, 15, y, title, COL_TEXT2);
  const int barX = 15, barY = y + 19, barW = 369, barH = 14;
  drawPercentBar(barX, barY, barW, barH, percent, color);
  drawText(FONT_MDB, barX + barW + 12, barY + (barH - fontLineH(FONT_MDB)) / 2,
           percent >= 0 ? String(percent) + "%" : String("--"), COL_TEXT);
  if (sub.length()) drawText(FONT_SM, 15, barY + barH + 2, sub, COL_TEXT2);
}

// Device Stats overlay: reached only by tapping the footer's CPU/ROM/RAM line
// (DEVICE_HIT_*); full-screen, no footer, any tap dismisses.
static void drawDevicePage() {
  drawText(FONT_LG, 15, 8, "Device Stats", COL_TEXT);

  int cpuInt = (int)(cpuPercentAvg + 0.5f);
  drawFullStatBlock(48, "CPU USAGE (render loop duty cycle, live)", cpuInt, "",
                    cpuInt >= 80 ? COL_WARN : COL_BLUE);

  uint32_t flashUsed, flashTotal;
  int flashPct = flashPercent(flashUsed, flashTotal);
  drawFullStatBlock(98, "FLASH (APP PARTITION)", flashPct,
                    fmtKB(flashUsed) + " / " + fmtKB(flashTotal), flashPct >= 80 ? COL_WARN : COL_BLUE);

  uint32_t ramUsed, ramTotal;
  int ramPct = staticRamPercent(ramUsed, ramTotal);
  drawFullStatBlock(152, "INTERNAL RAM", ramPct,
                    fmtKB(ramUsed) + " / " + fmtKB(ramTotal), ramPct >= 80 ? COL_WARN : COL_BLUE);

  uint32_t psUsed, psTotal;
  int psPct = psramPercent(psUsed, psTotal);
  drawFullStatBlock(206, "PSRAM", psPct,
                    fmtKB(psUsed) + " / " + fmtKB(psTotal), psPct >= 80 ? COL_WARN : COL_BLUE);

  uint64_t sdUsed = 0, sdTotal = 0;
  int sdPct = cachedSdCapacityPercent(sdUsed, sdTotal);
  drawFullStatBlock(260, "SD CARD", sdPct,
                    sdPct >= 0 ? fmtGB(sdUsed) + " / " + fmtGB(sdTotal) : String("SD CARD NOT FOUND"),
                    sdPct >= 80 ? COL_WARN : COL_BLUE);
}

// Weather detail overlay (tap the status-page weather card). Hero (icon ·
// temp · condition · H/L · AQI), hourly 6-col strip, 5-day range bars.
// Full 480x320, no footer; any tap dismisses. Twin of simulator-s3.html.
static void drawWeatherPage() {
  g->fillScreen(COL_BG);
  const int CARD_X = 15, CARD_W = 450, heroRight = CARD_X + CARD_W - 12;

  // ── Hero card (15, 5, 450, 72) ───────────────────────────
  drawCard(CARD_X, 5, CARD_W, 72);
  drawWeatherIcon(44, 41, STATE.weatherCode, 2.0f);
  const int tempX = 72;

  int metaX;
  if (STATE.weatherTempC > -900) {
    char tbuf[6];
    snprintf(tbuf, sizeof(tbuf), "%d", (int)round(STATE.weatherTempC));
    int x = drawText(FONT_XL, tempX, 16, tbuf, COL_TEXT);
    // Degree ring (drawn, like the CYD's raised "o") -- muted so digits dominate.
    g->drawCircle(x + 6, 24, 4, COL_TEXT2);
    g->drawCircle(x + 6, 24, 3, COL_TEXT2);
    metaX = x + 24;
  } else {
    int x = drawText(FONT_XL, tempX, 16, "--", COL_TEXT2);
    metaX = x + 20;
  }

  // AQI badge pinned to the hero card's top-right corner; the condition text
  // truncates around it.
  bool haveAqi = cfgShowAqi && STATE.aqi >= 0;
  int badgeW = haveAqi ? aqiBadgeW(STATE.aqi) : 0;
  const int metaMaxX = heroRight - (haveAqi ? badgeW + 10 : 0);

  {
    const char* cond = STATE.weatherCondition[0] ? STATE.weatherCondition : "--";
    char cbuf[20];
    int n = 0;
    // Truncate by measured width, not a character count (proportional font).
    while (cond[n] && n < (int)sizeof(cbuf) - 1) {
      cbuf[n] = cond[n];
      cbuf[n + 1] = 0;
      if (metaX + textW(FONT_MDB, cbuf) > metaMaxX) break;
      n++;
    }
    cbuf[n] = 0;
    drawText(FONT_MDB, metaX, 14, cbuf, STATE.weatherCondition[0] ? COL_TEXT : COL_TEXT2);
  }

  // High / Low -- "H 35   L 26", under the condition.
  {
    char hStr[5], lStr[5];
    if (STATE.weatherHigh > -900) snprintf(hStr, sizeof(hStr), "%d", STATE.weatherHigh);
    else snprintf(hStr, sizeof(hStr), "--");
    if (STATE.weatherLow > -900) snprintf(lStr, sizeof(lStr), "%d", STATE.weatherLow);
    else snprintf(lStr, sizeof(lStr), "--");
    int x = metaX;
    x = drawText(FONT_MD, x, 43, "H ", COL_TEXT2);
    x = drawText(FONT_MD, x, 43, hStr, COL_TEXT);
    x = drawText(FONT_MD, x + 16, 43, "L ", COL_TEXT2);
    drawText(FONT_MD, x, 43, lStr, COL_TEXT);
  }

  if (haveAqi) drawAqiBadge(heroRight - badgeW, 12, STATE.aqi);

  // ── Hourly (next 6) (15, 81, 450, 78) ────────────────────
  drawCard(CARD_X, 81, CARD_W, 78);
  const int hourCount = STATE.weatherHourlyCount > 0
                          ? (int)STATE.weatherHourlyCount : WEATHER_HOURLY_N;
  const int slotW = 75;  // 6 x 75 = 450
  for (int i = 0; i < hourCount && i < WEATHER_HOURLY_N; i++) {
    int sx = CARD_X + i * slotW;
    int cx = sx + slotW / 2;
    bool have = (i < (int)STATE.weatherHourlyCount);
    int hour = have ? STATE.weatherHourly[i].hour : -1;
    int temp = have ? STATE.weatherHourly[i].tempC : 0;
    int code = have ? STATE.weatherHourly[i].code : -1;

    // First slot = current hour: accent tick + accent hour label.
    if (i == 0) g->fillRoundRect(sx + 10, 83, slotW - 20, 3, 1, COL_ACCENT);

    char hbuf[4];
    if (have && hour >= 0) snprintf(hbuf, sizeof(hbuf), "%02d", hour);
    else snprintf(hbuf, sizeof(hbuf), "--");
    drawTextC(FONT_SM, cx, 89, hbuf, i == 0 ? COL_ACCENT : COL_TEXT2);

    drawWeatherIcon(cx, 118, code, 1.5f);

    if (have) {
      char tbuf[5];
      snprintf(tbuf, sizeof(tbuf), "%d", temp);
      drawTextC(FONT_MD, cx, 133, tbuf, COL_TEXT);
    } else {
      drawTextC(FONT_MD, cx, 133, "--", COL_TEXT2);
    }
  }

  // ── 5-day (15, 163, 450, 152) ────────────────────────────
  drawCard(CARD_X, 163, CARD_W, 152);

  int minT = 100, maxT = -100;
  for (uint8_t i = 0; i < STATE.weatherDailyCount; i++) {
    if (STATE.weatherDaily[i].low < minT) minT = STATE.weatherDaily[i].low;
    if (STATE.weatherDaily[i].high > maxT) maxT = STATE.weatherDaily[i].high;
  }
  if (maxT <= minT) {
    minT = 20;
    maxT = 40;
  }

  // Columns: day 28 | icon centre 84 | low right@136 | bar 144..408 | high 416
  const int dayY0 = 166;
  const int dayStep = 30;
  const int barX = 144, barW = 264, barH = 8;
  const int dayCount = STATE.weatherDailyCount > 0
                         ? (int)STATE.weatherDailyCount : WEATHER_DAILY_N;
  for (int i = 0; i < dayCount && i < WEATHER_DAILY_N; i++) {
    int y = dayY0 + i * dayStep;
    bool have = (i < (int)STATE.weatherDailyCount);
    int wd = have ? STATE.weatherDaily[i].wday : -1;
    int lo = have ? STATE.weatherDaily[i].low : 0;
    int hi = have ? STATE.weatherDaily[i].high : 0;
    int code = have ? STATE.weatherDaily[i].code : -1;

    // Vertically centre text/icon/bar in the 30px row.
    const int textY = y + (dayStep - fontLineH(FONT_MD)) / 2;
    const int barY = y + (dayStep - barH) / 2;

    drawText(FONT_MD, 28, textY, (have && wd >= 0 && wd < 7) ? WDAY_ABBR[wd] : "---",
             i == 0 ? COL_ACCENT : COL_TEXT);

    drawWeatherIcon(90, y + dayStep / 2, code, 1.2f);

    char lbuf[5], hbuf[5];
    if (have) {
      snprintf(lbuf, sizeof(lbuf), "%d", lo);
      snprintf(hbuf, sizeof(hbuf), "%d", hi);
    } else {
      snprintf(lbuf, sizeof(lbuf), "--");
      snprintf(hbuf, sizeof(hbuf), "--");
    }
    drawTextR(FONT_MD, barX - 8, textY, lbuf, COL_TEXT2);

    g->fillRoundRect(barX, barY, barW, barH, 4, COL_TRACK);
    if (have && maxT > minT) {
      float span = (float)(maxT - minT);
      int x0 = barX + (int)((lo - minT) / span * barW);
      int x1 = barX + (int)((hi - minT) / span * barW);
      if (x1 - x0 < barH) x1 = x0 + barH;
      g->fillRoundRect(x0, barY, x1 - x0, barH, 4, COL_ACCENT);
    }

    drawText(FONT_MD, barX + barW + 8, textY, hbuf, COL_TEXT);
  }
}

void render() {
  // The cat pages are animated frame-by-frame by gifTick() in loop(), not
  // drawn here; offline is also gifTick()'s (cats double as the offline
  // screen), so render() is never called while offline.
  if (currentPage == GIF_PAGE || currentPage == MIXED_PAGE) return;
  uint32_t startUs = micros();
  // Hold the lock across all STATE reads, release before the present.
  lockState();
  if (weatherPageOpen) {
    drawWeatherPage();
    drawBatterySaveIcon();
  } else if (devicePageOpen) {
    g->fillScreen(COL_BG);
    drawDevicePage();
    drawBatterySaveIcon();
  } else {
    g->fillScreen(COL_BG);
    switch (currentPage) {
      case 0: drawStatusPage(); break;
      case 1: drawProjectsPage(); break;  // projects (7d) + 7-day trend combined
      case 2: drawLimitsPage(); break;    // /usage-style limits panel
      case 5: drawNotePage(); break;      // left column + note text pane
    }
    drawFooter();
    drawBatterySaveIcon();
  }
  unlockState();
  uint32_t drawUs = micros() - startUs;
  presentFrame();
  // Permanent low-volume diagnostic (one line per render, only visible with a
  // serial monitor attached) so a draw-time regression shows up the same way
  // the CYD's did.
  static uint32_t lastLogMs = 0;
  if (millis() - lastLogMs >= 10000) {
    lastLogMs = millis();
    Serial.printf("[timing] render() page=%d draw %luus present %luus (te wait %luus)\n", currentPage,
                  (unsigned long)drawUs, (unsigned long)displayLastPresentUs(),
                  (unsigned long)displayLastTeWaitUs());
  }
}
