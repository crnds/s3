// Status strip (design.md 11.12): dot, Wi-Fi glyph, gear, page dots, progress hairline.
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"

// ── STATUS STRIP (design.md 11.12) ─────────────────────────
static const int PROGRESS_Y = SCREEN_H - 1;
static const int DOT_CX = 12, WIFI_X0 = 24;
static const int DOTS_W = PAGE_COUNT * 6 + (PAGE_COUNT - 1) * 6;
static const int GEAR_CX = 452;
static const int STRIP_PRESSED_R = 10;  // strip pressed states: 20px tall, the strip itself
static const int CPU_X = 47;  // just past the Wi-Fi glyph (x 24..39) + an 8px gap

// Status dot: server OK = success, filled; unreachable = warning ring;
// unknown / booting = tertiary ring. `r` is the steady radius, 4.
static void drawStatusDot(int r) {
  const uint16_t bg = (pressedId == PRESS_HEALTH) ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_BG_CANVAS;
  if (connected) {
    aaFillCircle(DOT_CX, STRIP_CY, r, TOK_COLOR_STATUS_SUCCESS);
  } else {
    aaFillCircle(DOT_CX, STRIP_CY, r, wifiOk ? TOK_COLOR_STATUS_WARNING : TOK_COLOR_TEXT_TERTIARY);
    aaFillCircle(DOT_CX, STRIP_CY, r - 2, bg);
  }
}

// Status-dot pulse (motion.pulse): while the server is reachable the dot
// breathes -- its opacity follows a cosine from 100% down to 1% and back
// once a period, free-running (never restarted, so it can't jump). Drawn as a
// sub-pixel disc at that alpha over the box's background every ambient tick.
static const int PULSE_BOX_R = 6;
static bool pulseDrawnLive = false;  // last pulseTick drew a breathing frame

static bool pulseLive() { return connected && !cfgReduceMotion; }

static uint8_t pulseAlpha(uint32_t nowMs) {
  uint32_t periodMs = (uint32_t)(TOK_MOTION_PULSE_PERIOD_MS * motionTimeScale);
  float t = (float)(nowMs % periodMs) / periodMs;
  float lo = TOK_MOTION_PULSE_MIN_PCT / 100.0f;
  return (uint8_t)(255.0f * (lo + (1.0f - lo) * (0.5f + 0.5f * cosf(2.0f * (float)M_PI * t))));
}

// The dot as of `nowMs`: the breathing disc while live, else the steady dot.
// Shared by the strip (so a 1 Hz render keeps the breath's phase) and
// pulseTick.
static void drawLiveDot(uint32_t nowMs) {
  if (!pulseLive()) { drawStatusDot(4); return; }
  aaRingF(DOT_CX, STRIP_CY, 4.5f, 0.0f, TOK_COLOR_STATUS_SUCCESS, pulseAlpha(nowMs));
}

// Wi-Fi: 3 bars, 3px wide with 2px gaps, heights 4/8/12, bottom-aligned.
// Down = the bars become 1px outlines (the shape changes, not just colour).
static void drawWifiGlyph() {
  const uint16_t c = wifiOk ? TOK_COLOR_STATUS_SUCCESS : TOK_COLOR_STATUS_ERROR;
  const int bottom = STRIP_CY + 6;
  for (int i = 0; i < 3; i++) {
    int h = 4 * (i + 1);
    int x = WIFI_X0 + 2 + i * 5;
    if (wifiOk) g->fillRect(x, bottom - h, 3, h, c);
    else g->drawRect(x, bottom - h, 3, h, c);
  }
}

static int progressLineW() {
  uint32_t elapsed = millis() - lastPollMs;
  if (elapsed > POLL_INTERVAL_MS) elapsed = POLL_INTERVAL_MS;
  return (int)((float)elapsed / POLL_INTERVAL_MS * SCREEN_W);
}
// How long one physical pixel of fill takes to appear at the current poll
// interval, floored at TOK_MOTION_AMBIENT_TICK_MS: the finest step the
// hairline can show is 1 px, so presenting any more often than that wastes a
// present; below the floor it grows several px per ambient tick.
static uint32_t progressStepMs() {
  uint32_t px = POLL_INTERVAL_MS / SCREEN_W;
  return px > TOK_MOTION_AMBIENT_TICK_MS ? px : TOK_MOTION_AMBIENT_TICK_MS;
}
static int progressDrawnW = 0;  // how much of the hairline is currently in `frame`
static uint32_t progressLastMs = 0;

void drawStatusStrip() {
  // Health cluster (-> Device Stats): pressed = a fill.pressed pill behind both glyphs.
  if (pressedId == PRESS_HEALTH)
    aaFillRoundRect(DOT_CX - 8, STRIP_CY - 10, 48, 20, 10, TOK_COLOR_FILL_PRESSED);
  drawLiveDot(millis());
  drawWifiGlyph();

  if (dataStale()) {
    // Stale data (design.md 13.4) takes the CPU readout's place, in its two
    // tones: "Updated" in text.secondary, the age in text.primary -- or "Saved
    // data" for the SD cache before the first good poll.
    if (lastFetchSuccessMs == 0) {
      drawText(TOK_TYPE_CAPTION, CPU_X, STRIP_CAPTION_Y, "Saved data", TOK_COLOR_TEXT_PRIMARY);
    } else {
      uint32_t ageMin = (millis() - lastFetchSuccessMs) / 60000;
      String age = ageMin < 60 ? String(ageMin) + "m ago" : String(ageMin / 60) + "h ago";
      int tx = drawText(TOK_TYPE_CAPTION, CPU_X, STRIP_CAPTION_Y, "Updated ", TOK_COLOR_TEXT_SECONDARY);
      drawText(TOK_TYPE_CAPTION, tx, STRIP_CAPTION_Y, age, TOK_COLOR_TEXT_PRIMARY);
    }
  } else {
    // CPU usage: render-loop duty cycle, "CPU" in text.secondary, the number in
    // text.primary, no separating space or bracket.
    int cpuInt = (int)(cpuPercentAvg + 0.5f);
    int cpuTx = drawText(TOK_TYPE_CAPTION, CPU_X, STRIP_CAPTION_Y, "CPU", TOK_COLOR_TEXT_SECONDARY);
    drawText(TOK_TYPE_CAPTION, cpuTx, STRIP_CAPTION_Y, String(cpuInt) + "%", TOK_COLOR_TEXT_PRIMARY);
  }

  // Page indicator: 6 dots, 6px across with 6px gaps, centred at x 240.
  int x = TOK_LAYOUT_HALF_SPLIT_X - DOTS_W / 2;
  for (int i = 0; i < PAGE_COUNT; i++, x += 12)
    aaFillRoundRect(x, STRIP_CY - 3, 6, 6, 3,
                           i == currentPage ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_FILL_TRACK);

  // Settings gear.
  bool gp = (pressedId == PRESS_GEAR);
  if (gp) aaFillCircle(GEAR_CX, STRIP_CY, STRIP_PRESSED_R, TOK_COLOR_FILL_PRESSED);  // the 40px disc would not fit a 20px strip
  drawGearGlyph(GEAR_CX, STRIP_CY, gp ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_SECONDARY,
                gp ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_BG_CANVAS);

  // Progress hairline (design.md 11.13): hidden offline and when switched off.
  progressDrawnW = 0;
  progressLastMs = millis();
  if (!cfgShowProgress || !connected) return;
  int w = progressLineW();
  if (w > 0) g->fillRect(0, PROGRESS_Y, w, 1, TOK_COLOR_TEXT_SECONDARY);
  progressDrawnW = w;
}

// Between-render top-up of the hairline: re-presents every time the fill
// would grow by 1 physical pixel (progressStepMs), the finest step it can
// show, but no more often than motion.ambient.tick (8 Hz, design.md 12.7) --
// at the default 20 s poll that is ~3 px a step. Only on pages with a strip
// (loop() gates it).
bool progressTick(uint32_t nowMs) {
  if (!cfgShowProgress || !connected) return false;
  if (nowMs - progressLastMs < progressStepMs()) return false;
  progressLastMs = nowMs;
  int w = progressLineW();
  if (w < progressDrawnW) {  // new poll cycle: clear and restart
    g->fillRect(0, PROGRESS_Y, SCREEN_W, 1, TOK_COLOR_BG_CANVAS);
    progressDrawnW = 0;
  }
  if (w <= progressDrawnW) return false;
  g->fillRect(progressDrawnW, PROGRESS_Y, w - progressDrawnW, 1, TOK_COLOR_TEXT_SECONDARY);
  progressDrawnW = w;
  return true;
}

// Between-render top-up of the pulse: repaints the dot's box (canvas, the
// health pill if pressed, then the live dot) on every ambient tick while live,
// plus once when it stops so the steady dot is left behind.
bool pulseTick(uint32_t nowMs) {
  bool live = pulseLive();
  if (!live && !pulseDrawnLive) return false;
  pulseDrawnLive = live;
  g->setClipRect(DOT_CX - PULSE_BOX_R, STRIP_CY - PULSE_BOX_R, 2 * PULSE_BOX_R + 1, 2 * PULSE_BOX_R + 1);
  g->fillRect(DOT_CX - PULSE_BOX_R, STRIP_CY - PULSE_BOX_R, 2 * PULSE_BOX_R + 1, 2 * PULSE_BOX_R + 1,
              TOK_COLOR_BG_CANVAS);
  if (pressedId == PRESS_HEALTH)
    aaFillRoundRect(DOT_CX - 8, STRIP_CY - 10, 48, 20, 10, TOK_COLOR_FILL_PRESSED);
  drawLiveDot(nowMs);
  g->clearClipRect();
  return true;
}
