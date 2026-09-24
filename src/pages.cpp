// Frame presentation (pixel shift) + every page's and sheet's draw function,
// built only from design.md's tokens (tokens.h) and components (section 11).
// This is the near-line-for-line twin of simulator-s3.html's page-drawing
// functions -- see CLAUDE.md's firmware/simulator parity rule before touching
// layout, colours, or text here without updating the simulator too.
//
// Ported from ~/cyd's pages.cpp: same pages, same information, same rules
// (pace flag, AQI colours, note tokenizer); the look is design.md's.
#include "state.h"

// Shared by the status-page date and the weather sheet's daily rows.
static const char* const WDAY_ABBR[7] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char* const MON_ABBR[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// ── PRESENTATION ───────────────────────────────────────────
bool presentHold = false;
volatile uint32_t teWaitAccumUs = 0;
volatile uint32_t pollOkSeq = 0;
// Set when a present was asked for while a transition owns the panel; nav.cpp
// composites it on its next tick.
bool transitionDirty = false;

void presentFrame() {
  if (presentHold) return;
  if (navTransitionActive()) { transitionDirty = true; return; }
  displayPresent((const uint16_t*)frame.getBuffer(), shiftX, shiftY, TOK_COLOR_BG_CANVAS);
  teWaitAccumUs += displayLastTeWaitUs();
  shiftDirty = false;
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

// ── ANTI-ALIASED FILLS ─────────────────────────────────────
// LovyanGFX's fillSmoothRoundRect algorithm (LGFXBase.inl: solid spans + 1px
// alpha edge pixels, 1/32 thresholds) with one change: edge pixels blend
// straight into the sprite's big-endian RGB565 buffer instead of going
// through the library's per-pixel readRect/writeImage effect path, which cost
// ~30 ms on the status page. The pixels are the same algorithm's; the
// simulator's gfx.fillSmoothRoundRect is the twin. Respects the clip rect.
static inline void blendPx(int x, int y, uint16_t c, uint8_t a) {
  int32_t cx, cy, cw, ch;
  g->getClipRect(&cx, &cy, &cw, &ch);
  if (x < cx || y < cy || x >= cx + cw || y >= cy + ch) return;
  uint16_t* p = (uint16_t*)frame.getBuffer() + y * SCREEN_W + x;
  uint16_t d = (uint16_t)((*p >> 8) | (*p << 8));
  uint32_t dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
  uint32_t sr = (c >> 11) & 31, sg = (c >> 5) & 63, sb = c & 31;
  dr = (dr << 3) | (dr >> 2); dg = (dg << 2) | (dg >> 4); db = (db << 3) | (db >> 2);
  sr = (sr << 3) | (sr >> 2); sg = (sg << 2) | (sg >> 4); sb = (sb << 3) | (sb >> 2);
  uint32_t inv = 256 - a, k = 1 + a;
  uint32_t r = (sr * k + dr * inv) >> 8, gg = (sg * k + dg * inv) >> 8, b = (sb * k + db * inv) >> 8;
  uint16_t o = (uint16_t)(((r >> 3) << 11) | ((gg >> 2) << 5) | (b >> 3));
  *p = (uint16_t)((o >> 8) | (o << 8));
}

void aaFillRoundRect(int x, int y, int w, int h, int r, uint16_t c) {
  if (w <= 0 || h <= 0) return;
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  y += r;
  h -= 2 * r;
  if (h > 0) g->fillRect(x, y, w, h, c);
  h--;
  x += r;
  w -= 2 * r + 1;
  const int r1 = r * r;
  r++;
  const int r2 = r * r;
  const float LO = 1.0f / 32.0f, HI = 1.0f - LO;
  int xs = 0, cx = 0;
  for (int cy = r - 1; cy > 0; cy--) {
    int dy2 = (r - cy) * (r - cy);
    for (cx = xs; cx < r; cx++) {
      int hyp2 = (r - cx) * (r - cx) + dy2;
      if (hyp2 <= r1) break;
      if (hyp2 >= r2) continue;
      float alphaf = (float)r - sqrtf((float)hyp2);
      if (alphaf > HI) break;
      xs = cx;
      if (alphaf < LO) continue;
      uint8_t a = (uint8_t)(alphaf * 255);
      blendPx(x + cx - r, y + cy - r, c, a);
      blendPx(x - cx + r + w, y + cy - r, c, a);
      blendPx(x - cx + r + w, y - cy + r + h, c, a);
      blendPx(x + cx - r, y - cy + r + h, c, a);
    }
    int len = 2 * (r - cx) + 1 + w;
    if (len > 0) {
      g->fillRect(x + cx - r, y + cy - r, len, 1, c);
      g->fillRect(x + cx - r, y - cy + r + h, len, 1, c);
    }
  }
}

void aaFillCircle(int x, int y, int r, uint16_t c) { aaFillRoundRect(x - r, y - r, r * 2 + 1, r * 2 + 1, r, c); }

// Anti-aliased ring of thickness t whose outer edge is radius r: only the
// band's pixels are touched (coverage = outer disc minus inner disc), so a
// big clock face costs ~1 ms instead of two full disc fills.
void aaRing(int cx, int cy, int r, int t, uint16_t c) {
  const float ro = r + 0.5f, ri = r - t + 0.5f;
  for (int dy = -r - 1; dy <= r + 1; dy++) {
    float xo2 = (ro + 1) * (ro + 1) - dy * dy;
    if (xo2 < 0) continue;
    int xo = (int)ceilf(sqrtf(xo2));
    float xi2 = (ri - 1) * (ri - 1) - dy * dy;
    int xi = xi2 > 0 ? (int)floorf(sqrtf(xi2)) : 0;
    for (int dx = xi; dx <= xo; dx++) {
      float d = sqrtf((float)(dx * dx + dy * dy));
      float cov = constrain(ro - d, 0.0f, 1.0f) - constrain(ri - d, 0.0f, 1.0f);
      if (cov < 1.0f / 32.0f) continue;
      uint8_t a = cov >= 1.0f ? 255 : (uint8_t)(cov * 255);
      blendPx(cx + dx, cy + dy, c, a);
      if (dx) blendPx(cx - dx, cy + dy, c, a);
    }
  }
}

// ── PRIMITIVES ─────────────────────────────────────────────
// Card (design.md 11.1): a surface step, radius.md, no border -- except the
// 1px gray.4 outline Increase Contrast adds.
void drawCardSurface(int x, int y, int w, int h, uint16_t fill) {
  aaFillRoundRect(x, y, w, h, TOK_RADIUS_MD, fill);
  if (TOK_CARD_OUTLINE) g->drawRoundRect(x, y, w, h, TOK_RADIUS_MD, TOK_COLOR_CARD_OUTLINE);
}
static void drawCard(int x, int y, int w, int h) { drawCardSurface(x, y, w, h, TOK_COLOR_SURFACE_CARD); }

// Section label (design.md 11.2): type.label, UPPERCASE, text.secondary.
static void drawSectionLabel(int x, int y, const char* label) {
  drawText(TOK_TYPE_LABEL, x, y, label, TOK_COLOR_TEXT_SECONDARY);
}

// Meter (design.md 11.4): fill.track + a data-series fill, radius.full; a
// non-zero value is at least a full-height stub; unknown = the track alone.
// Returns the fill width, or -1 when unknown.
static int drawMeter(int x, int y, int w, int h, int percent, uint16_t color) {
  aaFillRoundRect(x, y, w, h, h / 2, TOK_COLOR_FILL_TRACK);
  if (percent < 0) return -1;
  int fillW = (int)((float)min(percent, 100) / 100 * w + 0.5f);
  if (fillW < h) fillW = h;
  aaFillRoundRect(x, y, fillW, h, h / 2, color);
  return fillW;
}

// Text truncated on MEASURED width (design.md 5.3 rule 6): cut at a word
// boundary if one is within 4 characters of the cut, else hard.
static String fitText(FontId f, const char* s, int maxW) {
  if (textW(f, s) <= maxW) return String(s);
  String out(s);
  while (out.length() > 0 && textW(f, out) > maxW) out.remove(out.length() - 1);
  int sp = out.lastIndexOf(' ');
  if (sp > 0 && (int)out.length() - sp <= 4) out.remove(sp);
  return out;
}

// ── SYSTEM GLYPHS (design.md 10.3) ─────────────────────────
// Every one is built from drawWideLine / fillSmoothCircle / fillSmoothRoundRect
// at stroke.glyph (2px), centred on (cx, cy).
void drawCloseGlyph(int cx, int cy, uint16_t c) {
  g->drawWideLine(cx - 6, cy - 6, cx + 6, cy + 6, TOK_STROKE_GLYPH_R, c);
  g->drawWideLine(cx + 6, cy - 6, cx - 6, cy + 6, TOK_STROKE_GLYPH_R, c);
}
void drawBackGlyph(int cx, int cy, uint16_t c) {  // 8 wide x 14 tall, pointing left
  g->drawWideLine(cx + 4, cy - 7, cx - 4, cy, TOK_STROKE_GLYPH_R, c);
  g->drawWideLine(cx - 4, cy, cx + 4, cy + 7, TOK_STROKE_GLYPH_R, c);
}
void drawChevron(int cx, int cy, uint16_t c) {    // disclosure, 5 x 10, pointing right
  g->drawWideLine(cx - 2, cy - 5, cx + 3, cy, TOK_STROKE_GLYPH_R, c);
  g->drawWideLine(cx + 3, cy, cx - 2, cy + 5, TOK_STROKE_GLYPH_R, c);
}
static void drawGearGlyph(int cx, int cy, uint16_t c, uint16_t bg) {
  aaFillCircle(cx, cy, 6, c);   // ring r 5, 2px
  aaFillCircle(cx, cy, 4, bg);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    g->drawWideLine(cx + lroundf(cosf(a) * 7), cy + lroundf(sinf(a) * 7),
                    cx + lroundf(cosf(a) * 9), cy + lroundf(sinf(a) * 9), TOK_STROKE_GLYPH_R, c);
  }
}
// Icon button pressed state (design.md 11.10): a 40px fill.pressed disc.
void drawIconButtonPressed(int cx, int cy) {
  aaFillCircle(cx, cy, TOK_PRESSED_DISC_R, TOK_COLOR_FILL_PRESSED);
}

// Shuffle media control (design.md 11.20): two crossing arrows on a 32px
// plate disc; pressed = the disc steps to fill.pressed (same footprint, so
// nothing over the GIF needs restoring on release).
void drawShuffleButton(int cx, int cy, bool pressed) {
  aaFillCircle(cx, cy, SHUFFLE_DISC_R, pressed ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_PLATE);
  const uint16_t c = TOK_COLOR_TEXT_PRIMARY;
  const float r = TOK_STROKE_GLYPH_R;
  g->drawWideLine(cx - 7, cy - 4, cx - 3, cy - 4, r, c);   // upper-left run in
  g->drawWideLine(cx - 3, cy - 4, cx + 3, cy + 4, r, c);   // cross down
  g->drawWideLine(cx + 3, cy + 4, cx + 7, cy + 4, r, c);   // out, lower right
  g->drawWideLine(cx - 7, cy + 4, cx - 3, cy + 4, r, c);   // lower-left run in
  g->drawWideLine(cx - 3, cy + 4, cx + 3, cy - 4, r, c);   // cross up
  g->drawWideLine(cx + 3, cy - 4, cx + 7, cy - 4, r, c);   // out, upper right
  g->fillTriangle(cx + 8, cy - 4, cx + 5, cy - 7, cx + 5, cy - 1, c);  // arrowheads
  g->fillTriangle(cx + 8, cy + 4, cx + 5, cy + 1, cx + 5, cy + 7, c);
}

void shuffleCentre(bool mixed, int& cx, int& cy) {
  // 8px in from the media box's bottom-right: the full screen, or the pane.
  int x1 = mixed ? MIXED_GIF_X0 + MIXED_GIF_W : SCREEN_W;
  int y1 = mixed ? MIXED_GIF_Y0 + MIXED_GIF_H : SCREEN_H;
  cx = x1 - TOK_SPACE_SM - SHUFFLE_DISC_R - 1;
  cy = y1 - TOK_SPACE_SM - SHUFFLE_DISC_R - 1;
}

// ── SYSTEM CORNER (design.md 7.3) ──────────────────────────
// Slot a: the sleep icon button (a crescent on a raised 24px disc; pressed =
// the disc steps to fill.pressed). Slot b: the Battery Save glyph, only while
// active. Drawn last on every screen; over media each occupied slot sits on a
// plate with a 4px inset around its glyph box.
void drawSystemCorner(bool overMedia) {
  const int ay = TOK_CORNER_SLOT_Y, sz = TOK_CORNER_SLOT_SIZE;
  if (batterySaveActive()) {
    const int bx = TOK_CORNER_SLOT_B_X;
    if (overMedia) g->fillRect(bx - 4, ay - 4, sz + 8, sz + 8, TOK_COLOR_PLATE);
    const uint16_t c = TOK_COLOR_STATUS_WARNING;
    // Body 16x10 (radius 2) + a 2x4 nub, 2px outline, filled at 50%.
    const int x = bx + 3, y = ay + 7;
    g->drawRoundRect(x, y, 16, 10, 2, c);
    g->drawRoundRect(x + 1, y + 1, 14, 8, 1, c);
    g->fillRect(x + 3, y + 3, 5, 4, c);
    g->fillRect(x + 16, y + 3, 2, 4, c);
  }
  const int ax = TOK_CORNER_SLOT_A_X;
  if (overMedia) g->fillRect(ax - 4, ay - 4, sz + 8, sz + 8, TOK_COLOR_PLATE);
  const int cx = ax + sz / 2, cy = ay + sz / 2;
  const bool pressed = (pressedId == PRESS_SLEEP);
  const uint16_t disc = pressed ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_SURFACE_RAISED;
  aaFillCircle(cx, cy, 11, disc);
  aaFillCircle(cx, cy, 7, pressed ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_SECONDARY);
  aaFillCircle(cx + 3, cy - 3, 6, disc);  // the bite that makes the crescent
}

// ── MODAL HEADER (design.md 11.14) ─────────────────────────
// Close (root) or back (deeper) in the close slot, the title in headline at
// x 60. No divider -- the scroll-edge shade does that job.
void drawModalHeader(bool back, const char* title, bool pressed) {
  const int cx = TOK_CLOSE_SLOT_X + 12, cy = TOK_CLOSE_SLOT_Y + 12;
  if (pressed) drawIconButtonPressed(cx, cy);
  uint16_t c = pressed ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_SECONDARY;
  if (back) drawBackGlyph(cx, cy, c);
  else drawCloseGlyph(cx, cy, c);
  if (title) drawText(TOK_TYPE_HEADLINE, TOK_HEADER_TITLE_X, TOK_HEADER_TITLE_Y, title, TOK_COLOR_TEXT_PRIMARY);
}

// ── STATUS STRIP (design.md 11.12) ─────────────────────────
static const int PROGRESS_Y = SCREEN_H - 1;
static const int DOT_CX = 12, WIFI_X0 = 24;
static const int DOTS_W = PAGE_COUNT * 6 + (PAGE_COUNT - 1) * 6;
static const int GEAR_CX = 452;

// Status dot: server OK = success, filled; unreachable = warning ring;
// unknown / booting = tertiary ring. `r` is the pulse radius (4, or 5 for a
// pulse frame).
static void drawStatusDot(int r) {
  const uint16_t bg = (pressedId == PRESS_HEALTH) ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_BG_CANVAS;
  if (connected) {
    aaFillCircle(DOT_CX, STRIP_CY, r, TOK_COLOR_STATUS_SUCCESS);
  } else {
    aaFillCircle(DOT_CX, STRIP_CY, r, wifiOk ? TOK_COLOR_STATUS_WARNING : TOK_COLOR_TEXT_TERTIARY);
    aaFillCircle(DOT_CX, STRIP_CY, r - 2, bg);
  }
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
static int progressDrawnW = 0;  // how much of the hairline is currently in `frame`
static uint32_t progressLastMs = 0;

static void drawStatusStrip() {
  // Health cluster (-> Device Stats): pressed = a fill.pressed pill behind both glyphs.
  if (pressedId == PRESS_HEALTH)
    aaFillRoundRect(DOT_CX - 8, STRIP_CY - 12, 48, 24, 12, TOK_COLOR_FILL_PRESSED);
  drawStatusDot(4);
  drawWifiGlyph();

  // Page indicator: 6 dots, 6px across with 6px gaps, centred at x 240.
  int x = TOK_LAYOUT_HALF_SPLIT_X - DOTS_W / 2;
  for (int i = 0; i < PAGE_COUNT; i++, x += 12)
    aaFillRoundRect(x, STRIP_CY - 3, 6, 6, 3,
                           i == currentPage ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_FILL_TRACK);

  // Settings gear.
  bool gp = (pressedId == PRESS_GEAR);
  if (gp) drawIconButtonPressed(GEAR_CX, STRIP_CY);
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

// Between-render top-up of the hairline, capped at motion.progress.maxHz
// (4 Hz): at a 20s poll it would otherwise present ~24 times a second for a
// single line. Only on pages with a strip (loop() gates it).
bool progressTick(uint32_t nowMs) {
  if (!cfgShowProgress || !connected) return false;
  if (nowMs - progressLastMs < TOK_MOTION_PROGRESS_MIN_MS) return false;
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

// Status-dot pulse (motion.pulse): r 4 -> 5 -> 4 over 3 frames on each
// successful poll -- "data arrived", instead of the old 1 Hz blink.
static const int8_t PULSE_R[TOK_MOTION_PULSE_FRAMES] = {5, 5, 4};
static int pulseFrame = -1;
static uint32_t pulseSeenSeq = 0;

bool pulseTick(uint32_t nowMs) {
  (void)nowMs;
  if (pollOkSeq != pulseSeenSeq) {
    pulseSeenSeq = pollOkSeq;
    if (!cfgReduceMotion) pulseFrame = 0;
  }
  if (pulseFrame < 0) return false;
  const uint16_t bg = (pressedId == PRESS_HEALTH) ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_BG_CANVAS;
  g->fillRect(DOT_CX - 6, STRIP_CY - 6, 13, 13, bg);
  drawStatusDot(PULSE_R[pulseFrame]);
  if (++pulseFrame >= TOK_MOTION_PULSE_FRAMES) pulseFrame = -1;
  return true;
}

// ── DATA HELPERS ───────────────────────────────────────────
// Live countdown: tick the last server-provided remaining-seconds down by
// wall time since lastFetchOkMs.
static long liveResetsInSec(long baseSec) {
  if (baseSec < 0) return -1;
  long rem = baseSec - (long)((millis() - STATE.lastFetchOkMs) / 1000);
  return rem < 0 ? 0 : rem;
}

// ── LIMITS PAGE (page 2) ───────────────────────────────────
// One card: the /usage panel -- context window, 5-hour, weekly (all models),
// weekly per-model (hidden when the server sends null), usage credits. Each
// row: label (body) left, detail (body, secondary) + the percent (headline)
// right, a meter.md under it. Rows close up when one is absent.
static const int LIM_X = TOK_LAYOUT_CONTENT_X0 + TOK_SPACE_CARD_PAD;   // 20
static const int LIM_R = TOK_LAYOUT_CONTENT_X1 - TOK_SPACE_CARD_PAD;   // 460 (exclusive)
static const int ROW_H = 23 + TOK_SPACE_STACK_TIGHT + TOK_METER_MD;    // 35
static const int ROW_STEP = ROW_H + TOK_SPACE_STACK;                   // 43

static void drawDataRow(int x, int r, int y, const String& label, const String& detail,
                        int percent, uint16_t meterColor) {
  drawText(TOK_TYPE_BODY, x, y, label, TOK_COLOR_TEXT_PRIMARY);
  String pct = percent >= 0 ? String(percent) + "%" : String("--");
  drawTextR(TOK_TYPE_NUMERAL_MD, r, y, pct, percent >= 0 ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  int vx = r - textW(TOK_TYPE_NUMERAL_MD, pct) - TOK_SPACE_SM;
  if (detail.length()) drawTextR(TOK_TYPE_BODY, vx, y, detail, TOK_COLOR_TEXT_SECONDARY);
  drawMeter(x, y + 23 + TOK_SPACE_STACK_TIGHT, r - x, TOK_METER_MD, percent, meterColor);
}

static String resetsDetail(const char* resets) {
  return resets[0] != '\0' ? String("Resets ") + resets : String("");
}

static void drawLimitsPage() {
  drawCard(TOK_LAYOUT_CONTENT_X0, TOK_LAYOUT_CONTENT_Y0, TOK_LAYOUT_CONTENT_W,
           TOK_LAYOUT_CONTENT_Y1 - TOK_LAYOUT_CONTENT_Y0);
  const int top = TOK_LAYOUT_CONTENT_Y0 + TOK_SPACE_CARD_PAD;
  drawSectionLabel(LIM_X, top, "USAGE LIMITS");

  int y = top + 17 + TOK_SPACE_SM;
  drawDataRow(LIM_X, LIM_R, y, "Context window",
              STATE.ctxTokens >= 0 ? fmtTokens(STATE.ctxTokens) : String(""),
              STATE.ctxTokens >= 0 ? STATE.ctxPercent : -1, TOK_COLOR_DATA_USAGE);
  y += ROW_STEP;
  drawDataRow(LIM_X, LIM_R, y, "5-hour limit", resetsDetail(STATE.sessionResets),
              STATE.sessionPercent, TOK_COLOR_DATA_USAGE);
  y += ROW_STEP;
  drawDataRow(LIM_X, LIM_R, y, "Weekly (all models)", resetsDetail(STATE.weekResets),
              STATE.weekPercent, TOK_COLOR_DATA_USAGE);
  y += ROW_STEP;
  if (STATE.weekModelPercent >= 0) {
    String name = STATE.weekModelName[0] != '\0' ? String(STATE.weekModelName) : String("model");
    drawDataRow(LIM_X, LIM_R, y, "Weekly (" + name + ")", resetsDetail(STATE.weekModelResets),
                STATE.weekModelPercent, TOK_COLOR_DATA_USAGE);
    y += ROW_STEP;
  }
  if (STATE.creditsUsed >= 0) {
    drawDataRow(LIM_X, LIM_R, y, "Usage credits",
                fmtCost(STATE.creditsUsed) + " of " + fmtCost(STATE.creditsLimit),
                STATE.creditsPercent, TOK_COLOR_DATA_USAGE);
  }
}

// ── PROJECTS PAGE (page 1) ─────────────────────────────────
// Two cards (one idea each): top projects, then the 7-day trend. Project rows
// are single-line (design.md 11.6's dense variant): name (body) in the left
// column, a meter.md from the right column's edge, value (caption) right.
static const int PROJ_CARD_H = 165, TREND_CARD_H = 99;
static const int TREND_CARD_Y = TOK_LAYOUT_CONTENT_Y0 + PROJ_CARD_H + TOK_SPACE_GUTTER;  // 181
static const int PROJ_ROW_STEP = 23 + TOK_SPACE_STACK;                                  // 31
static const int PROJ_VALUE_W = 56;

// Empty state (design.md 13.4), centred in a box: title (headline) +
// description (caption). isError swaps the title to status.error.
void drawEmptyState(int cx, int y0, int h, const char* title, const char* desc, bool isError,
                    FontId titleFont, uint16_t titleColor) {
  int th = fontLineH(titleFont);
  int total = th + TOK_SPACE_SM + 17;
  int y = y0 + (h - total) / 2;
  drawTextC(titleFont, cx, y, title, isError ? TOK_COLOR_STATUS_ERROR : titleColor);
  drawTextC(TOK_TYPE_CAPTION, cx, y + th + TOK_SPACE_SM, desc, TOK_COLOR_TEXT_SECONDARY);
}

static void drawProjectsPage() {
  const int x0 = TOK_LAYOUT_CONTENT_X0, w = TOK_LAYOUT_CONTENT_W;
  drawCard(x0, TOK_LAYOUT_CONTENT_Y0, w, PROJ_CARD_H);
  const int px = x0 + TOK_SPACE_CARD_PAD, pr = x0 + w - TOK_SPACE_CARD_PAD;
  const int top = TOK_LAYOUT_CONTENT_Y0 + TOK_SPACE_CARD_PAD;
  drawSectionLabel(px, top, "TOP PROJECTS 7D");

  if (STATE.projectCount == 0) {
    int ly = top + 17;
    drawEmptyState(x0 + w / 2, ly, TOK_LAYOUT_CONTENT_Y0 + PROJ_CARD_H - ly, "No project data yet",
                   "Projects appear after the first poll", false);
  } else {
    int shown = STATE.projectCount < 4 ? STATE.projectCount : 4;
    int64_t maxTokens = 1;
    for (int i = 0; i < shown; i++)
      if (STATE.projectTokens[i] > maxTokens) maxTokens = STATE.projectTokens[i];
    const int meterX = TOK_LAYOUT_COL_RIGHT_X;
    const int meterW = pr - PROJ_VALUE_W - TOK_SPACE_SM - meterX;
    const int nameW = meterX - TOK_SPACE_SM - px;
    int y = top + 17 + TOK_SPACE_SM;
    for (int i = 0; i < shown; i++, y += PROJ_ROW_STEP) {
      drawText(TOK_TYPE_BODY, px, y, fitText(TOK_TYPE_BODY, STATE.projectNames[i], nameW), TOK_COLOR_TEXT_PRIMARY);
      int pct = (int)((float)STATE.projectTokens[i] / maxTokens * 100 + 0.5f);
      drawMeter(meterX, y + 12 - TOK_METER_MD / 2, meterW, TOK_METER_MD, pct, TOK_COLOR_DATA_USAGE);
      // Caption value on the name's baseline.
      drawTextR(TOK_TYPE_NUMERAL_SM, pr, y + fontAscent(TOK_TYPE_BODY) - fontAscent(TOK_TYPE_CAPTION),
                fmtTokens(STATE.projectTokens[i]), TOK_COLOR_TEXT_SECONDARY);
    }
  }

  // ── 7-day trend (compact card): label column left, bars from x 188 ──
  drawCard(x0, TREND_CARD_Y, w, TREND_CARD_H);
  const int ty = TREND_CARD_Y + TOK_SPACE_CARD_PAD_COMPACT_V;
  drawSectionLabel(px, ty, "7-DAY TREND");
  int64_t total = 0, maxTrend = 1;
  for (int i = 0; i < 7; i++) {
    total += STATE.trend[i];
    if (STATE.trend[i] > maxTrend) maxTrend = STATE.trend[i];
  }
  drawText(TOK_TYPE_BODY, px, ty + 17 + TOK_SPACE_SM, fmtTokens(total), TOK_COLOR_TEXT_PRIMARY);
  drawText(TOK_TYPE_CAPTION, px, ty + 17 + TOK_SPACE_SM + 23, "tokens this week", TOK_COLOR_TEXT_SECONDARY);

  static const char* const DAY_LABELS[7] = {"-6", "-5", "-4", "-3", "-2", "-1", "Today"};
  const int barW = 32, gap = TOK_SPACE_SM;
  const int axisY = TREND_CARD_Y + TREND_CARD_H - TOK_SPACE_CARD_PAD_COMPACT_V - 17;  // 255
  const int feet = axisY - TOK_SPACE_XS;                                               // bars end above y 251
  const int chartH = feet - ty;
  for (int i = 0; i < 7; i++) {
    int bx = TOK_LAYOUT_COL_RIGHT_X + i * (barW + gap);
    int bh = (int)((float)STATE.trend[i] / maxTrend * chartH);
    if (bh < 4) bh = 4;
    aaFillRoundRect(bx, feet - bh, barW, bh, TOK_RADIUS_SM, TOK_COLOR_DATA_USAGE);
    drawTextC(TOK_TYPE_CAPTION, bx + barW / 2, axisY, DAY_LABELS[i],
              i == 6 ? TOK_COLOR_ACCENT : TOK_COLOR_TEXT_TERTIARY);
  }
}

// ── WEATHER GLYPHS (content family, design.md 10) ──────────
// Filled vector shapes mapped from Open-Meteo's WMO weather_code, CENTRED on
// (cx, cy) and scaled by k from the CYD's ~18px geometry.
static void drawWeatherIcon(int cx, int cy, int code, float k) {
  auto S = [k](float v) { return (int)lroundf(v * k); };
  float lw = 1.0f * k;
  if (code < 0) {
    drawTextC(TOK_TYPE_CAPTION, cx, cy - fontLineH(TOK_TYPE_CAPTION) / 2, "--", TOK_COLOR_TEXT_TERTIARY);
    return;
  }
  const uint16_t sun = TOK_COLOR_CONTENT_SUN;
  if (code == 0 || code == 1) {
    // clear: sun disc + 8 short rounded rays with a gap between disc and rays.
    aaFillCircle(cx, cy, S(4), sun);
    g->drawWideLine(cx, cy - S(9), cx, cy - S(7), lw, sun);
    g->drawWideLine(cx, cy + S(7), cx, cy + S(9), lw, sun);
    g->drawWideLine(cx - S(9), cy, cx - S(7), cy, lw, sun);
    g->drawWideLine(cx + S(7), cy, cx + S(9), cy, lw, sun);
    g->drawWideLine(cx - S(7), cy - S(7), cx - S(5), cy - S(5), lw, sun);
    g->drawWideLine(cx + S(5), cy + S(5), cx + S(7), cy + S(7), lw, sun);
    g->drawWideLine(cx - S(7), cy + S(7), cx - S(5), cy + S(5), lw, sun);
    g->drawWideLine(cx + S(5), cy - S(5), cx + S(7), cy - S(7), lw, sun);
    return;
  }
  // Rain/snow/lightning are drawn standalone (no cloud underneath) so the
  // condition itself reads clearly at small sizes.
  if (code >= 95) {
    // thunderstorm: zigzag bolt as 4 triangles. Vertices:
    // A(-1,+8) B(-1,+2) C(-5,+2) D(+1,-8) E(+1,-2) F(+5,-2).
    g->fillTriangle(cx - S(5), cy + S(2), cx + S(1), cy - S(8), cx + S(1), cy - S(2), sun);
    g->fillTriangle(cx - S(5), cy + S(2), cx + S(1), cy - S(2), cx - S(1), cy + S(2), sun);
    g->fillTriangle(cx - S(1), cy + S(2), cx + S(1), cy - S(2), cx + S(5), cy - S(2), sun);
    g->fillTriangle(cx - S(1), cy + S(2), cx + S(5), cy - S(2), cx - S(1), cy + S(8), sun);
    return;
  }
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    // snow: six-armed snowflake = three rounded lines crossing at 60deg
    const uint16_t c = TOK_COLOR_CONTENT_SNOW;
    g->drawWideLine(cx, cy - S(7), cx, cy + S(7), lw, c);
    g->drawWideLine(cx - S(6), cy - S(4), cx + S(6), cy + S(4), lw, c);
    g->drawWideLine(cx - S(6), cy + S(4), cx + S(6), cy - S(4), lw, c);
    return;
  }
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    // rain: three staggered teardrops, two small on top, one large below.
    const uint16_t c = TOK_COLOR_CONTENT_RAIN;
    g->fillTriangle(cx - S(6), cy - S(8), cx - S(8), cy - S(3), cx - S(4), cy - S(3), c);
    aaFillCircle(cx - S(6), cy - S(3), S(2), c);
    g->fillTriangle(cx + S(5), cy - S(6), cx + S(3), cy - S(1), cx + S(7), cy - S(1), c);
    aaFillCircle(cx + S(5), cy - S(1), S(2), c);
    g->fillTriangle(cx - S(1), cy + S(1), cx - S(4), cy + S(6), cx + S(2), cy + S(6), c);
    aaFillCircle(cx - S(1), cy + S(6), S(3), c);
    return;
  }
  // Everything else shares a plain cloud (2/3/45/48 = cloudy/fog, or any
  // unmapped code): two overlapping puffs on a fully-rounded pill base.
  const uint16_t c = TOK_COLOR_CONTENT_CLOUD;
  aaFillCircle(cx - S(4), cy - S(2), S(4), c);
  aaFillCircle(cx + S(3), cy - S(3), S(5), c);
  aaFillRoundRect(cx - S(9), cy - S(2), S(19), S(9), S(4), c);
}

// Degree ring (design.md 10.3): r 4 / r 3, text.secondary, top-aligned to
// the cap height of the number it follows. Returns the x past it.
static int drawDegreeRing(int x, int capTop, uint16_t bg) {
  aaFillCircle(x + 4, capTop + 4, 4, TOK_COLOR_TEXT_SECONDARY);
  aaFillCircle(x + 4, capTop + 4, 2, bg);
  return x + 9;
}

// Compact temperature: the value, then "C" in caption + secondary on the
// same baseline (design.md 5.3 rule 4). Centred on cx.
static void drawTempC(FontId f, int cx, int y, int t, bool have) {
  if (!have) { drawTextC(f, cx, y, "--", TOK_COLOR_TEXT_TERTIARY); return; }
  String v = String(t);
  int w = textW(f, v) + textW(TOK_TYPE_CAPTION, "C");
  int x = drawText(f, cx - w / 2, y, v, TOK_COLOR_TEXT_PRIMARY);
  drawText(TOK_TYPE_CAPTION, x, y + fontAscent(f) - fontAscent(TOK_TYPE_CAPTION), "C", TOK_COLOR_TEXT_SECONDARY);
}

// ── PACE SWEEP (motion.sweep) ──────────────────────────────
// One light band per successful poll across the green pace fills of the
// shared left column (status, mixed, note pages), then still: it means
// "fresh data". Constant speed, 600 ms. Twin of simulator-s3.html's.
static const int SHINE_BAND_R = 10;
static const int SHINE_BAR_X = TOK_LAYOUT_COL_LEFT_X + TOK_SPACE_CARD_PAD_COMPACT_H;   // 20
static const int SHINE_BAR_W = TOK_LAYOUT_COL_LEFT_W - 2 * TOK_SPACE_CARD_PAD_COMPACT_H;  // 148
static const int SHINE_BAR_H = TOK_METER_SM;
static int shineBarY[2] = {-1, -1};           // set by drawLimitCard (pace meter tops)
static volatile int shineFillPx[2] = {-1, -1};  // fill widths cached under stateMutex
static uint32_t sweepStartMs = 0;
static bool sweepActive = false;
static uint32_t sweepSeenSeq = 0;
static int shinePrevCenter[2] = {INT_MIN, INT_MIN};

static inline uint16_t shineColor(int i, int center) {
  int d = abs(i - center);
  return d <= 2 ? TOK_GREEN_SHINE_HI : d <= 6 ? TOK_GREEN_SHINE_MID
       : d <= SHINE_BAND_R ? TOK_GREEN_SHINE_LO : TOK_COLOR_DATA_PACE;
}

// Paint the band over one pace meter's filled interior (columns 2..fillW-3,
// so the round ends stay untouched). Returns true if any column changed.
static bool drawShineStrip(int barIdx, int center, bool force) {
  int fillW = shineFillPx[barIdx], y = shineBarY[barIdx];
  if (fillW < 14 || y < 0) return false;
  int prev = force ? INT_MIN : shinePrevCenter[barIdx];
  if (prev == center) return false;
  shinePrevCenter[barIdx] = center;
  bool changed = false;
  for (int i = 2; i <= fillW - 3; i++) {
    uint16_t c = shineColor(i, center);
    if (prev != INT_MIN && c == shineColor(i, prev)) continue;
    g->fillRect(SHINE_BAR_X + i, y, 1, SHINE_BAR_H, c);
    changed = true;
  }
  return changed;
}

static bool leftColumnPage() {
  return currentPage == 0 || currentPage == MIXED_PAGE || currentPage == NOTE_PAGE;
}

static int sweepCenter(uint32_t nowMs) {
  float t = (float)(nowMs - sweepStartMs) / (TOK_MOTION_SWEEP_MS * motionTimeScale);
  if (t > 1.0f) t = 1.0f;
  return -SHINE_BAND_R + (int)(t * (SHINE_BAR_W + 2 * SHINE_BAND_R));
}

bool shineTick(uint32_t nowMs) {
  if (pollOkSeq != sweepSeenSeq) {
    sweepSeenSeq = pollOkSeq;
    if (!cfgReduceMotion && cfgShowCountdown && leftColumnPage()) {
      sweepActive = true;
      sweepStartMs = nowMs;
      shinePrevCenter[0] = shinePrevCenter[1] = INT_MIN;
    }
  }
  if (!sweepActive) return false;
  if (!leftColumnPage() || !cfgShowCountdown) { sweepActive = false; return false; }
  int center = sweepCenter(nowMs);
  bool done = center >= SHINE_BAR_W + SHINE_BAND_R;
  bool changed = false;
  for (int i = 0; i < 2; i++)
    if (drawShineStrip(i, center, false)) changed = true;
  if (done) sweepActive = false;
  return changed;
}

// ── SHARED LEFT COLUMN (status / mixed / note pages) ───────
static const long SESSION_WINDOW_SEC = 5L * 3600;       // 5h
static const long WEEK_WINDOW_SEC = 7L * 24 * 3600;     // 168h

// Pace = % of the window elapsed: 0% right after a reset, 100% right before
// the next.
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

// Metric flag (design.md 11.3): "!" (headline, status.error) + the projection
// (body, secondary) on the value's baseline -- earned only by pace, never by
// level (callers gate `ahead` on actual > pace + deadband).
static void drawPaceFlag(int x, int baseline, bool ahead, int currentPct, long elapsedSec, long remainingSec) {
  if (!ahead) return;
  int x2 = drawText(TOK_TYPE_HEADLINE, x, baseline - fontAscent(TOK_TYPE_HEADLINE), "!", TOK_COLOR_STATUS_ERROR);
  String dur = formatPaceDur(currentPct, elapsedSec, remainingSec);
  if (dur.length() > 0)
    drawText(TOK_TYPE_BODY, x2 + TOK_SPACE_HAIR, baseline - fontAscent(TOK_TYPE_BODY), dur, TOK_COLOR_TEXT_SECONDARY);
}

// One limit card (design.md 7.4): numeral.lg % + label (+ flag), the paired
// meter (usage over pace), then two caption lines -- when it resets, and in
// how long.
static void drawLimitCard(int cardY, const char* label, int percent, bool ahead, long elapsed, long rem,
                          int pace, int barIdx, const char* resets, const String& inText) {
  drawCard(TOK_LAYOUT_COL_LEFT_X, cardY, TOK_LAYOUT_COL_LEFT_W, LIMIT_CARD_H);
  const int x = SHINE_BAR_X;
  const int top = cardY + TOK_SPACE_CARD_PAD_COMPACT_V;
  const int baseline = top + fontAscent(TOK_TYPE_NUMERAL_LG);
  int xe;
  if (percent >= 0) xe = drawText(TOK_TYPE_NUMERAL_LG, x, top, String(percent) + "%", TOK_COLOR_DATA_USAGE);
  else xe = drawText(TOK_TYPE_NUMERAL_LG, x, top, "--", TOK_COLOR_TEXT_TERTIARY);
  int lx = xe + TOK_SPACE_SM;
  drawSectionLabel(lx, baseline - fontAscent(TOK_TYPE_LABEL), label);
  drawPaceFlag(lx + textW(TOK_TYPE_LABEL, label) + TOK_SPACE_XS, baseline, ahead, percent, elapsed, rem);

  int y = top + fontLineH(TOK_TYPE_NUMERAL_LG) + TOK_SPACE_STACK_TIGHT;
  drawMeter(x, y, SHINE_BAR_W, TOK_METER_MD, percent, TOK_COLOR_DATA_USAGE);
  y += TOK_METER_MD + TOK_SPACE_STACK_TIGHT;
  // The pace meter is optional (Settings > Pace bars); off, the space closes up.
  if (cfgShowCountdown) {
    shineBarY[barIdx] = y;
    shineFillPx[barIdx] = drawMeter(x, y, SHINE_BAR_W, SHINE_BAR_H, pace, TOK_COLOR_DATA_PACE);
    if (sweepActive) drawShineStrip(barIdx, sweepCenter(millis()), true);
    y += SHINE_BAR_H + TOK_SPACE_STACK_TIGHT;
  } else {
    shineBarY[barIdx] = -1;
    shineFillPx[barIdx] = -1;
  }

  int rx = drawText(TOK_TYPE_CAPTION, x, y, "Resets ", TOK_COLOR_TEXT_SECONDARY);
  if (resets[0] != '\0') drawText(TOK_TYPE_CAPTION, rx, y, resets, TOK_COLOR_TEXT_SECONDARY);
  else drawText(TOK_TYPE_CAPTION, rx, y, "--", TOK_COLOR_TEXT_TERTIARY);
  if (inText.length()) drawText(TOK_TYPE_CAPTION, x, y + 17, inText, TOK_COLOR_TEXT_SECONDARY);
}

// Week reset label: the server's "Oct 1, 04:59" with the date swapped for the
// weekday name ("Thu 04:59"), computed locally from the live countdown so it
// never depends on the server's clock. Falls back to the server string
// verbatim when the countdown or the comma (time-of-day suffix) is missing.
static String weekResetWeekdayLabel(long weekRem, const char* resets) {
  if (resets[0] == '\0') return String(resets);
  const char* comma = strchr(resets, ',');
  if (weekRem < 0 || !comma) return String(resets);
  time_t resetT = time(nullptr) + weekRem;
  struct tm ti;
  localtime_r(&resetT, &ti);
  if (ti.tm_wday < 0 || ti.tm_wday > 6) return String(resets);
  const char* t = comma + 1;
  while (*t == ' ') t++;
  return String(WDAY_ABBR[ti.tm_wday]) + " " + t;
}

static void drawLimitsColumn() {
  long sessionRem = liveResetsInSec(STATE.sessionResetsInSec);
  long weekRem = liveResetsInSec(STATE.weekResetsInSec);
  String weekResetLabel = weekResetWeekdayLabel(weekRem, STATE.weekResets);

  // QUOTA PACING: "% of the window elapsed" (the pace meter) vs actual
  // usage -- a flag is earned only by running ahead of pace.
  int sessionPace = elapsedPercentOfWindow(sessionRem, SESSION_WINDOW_SEC);
  int weekPace = elapsedPercentOfWindow(weekRem, WEEK_WINDOW_SEC);
  bool sessionAhead = STATE.sessionPercent >= 0 && sessionPace >= 0 &&
                       STATE.sessionPercent > sessionPace + 2;  // 2pt deadband stops boundary flicker
  bool weekAhead = STATE.weekPercent >= 0 && weekPace >= 0 &&
                    STATE.weekPercent > weekPace + 2;
  long sessionElapsed = sessionRem >= 0 ? SESSION_WINDOW_SEC - sessionRem : -1;
  long weekElapsed = weekRem >= 0 ? WEEK_WINDOW_SEC - weekRem : -1;

  drawLimitCard(LIMIT5H_Y, "5H", STATE.sessionPercent, sessionAhead, sessionElapsed, sessionRem,
                sessionPace, 0, STATE.sessionResets,
                sessionRem >= 0 ? "in " + fmtCountdown(sessionRem) : String(""));
  drawLimitCard(LIMITWK_Y, "WK", STATE.weekPercent, weekAhead, weekElapsed, weekRem,
                weekPace, 1, weekResetLabel.c_str(),
                weekRem >= 0 ? "in " + fmtCountdownDHM(weekRem) : String(""));
}

// BTC (compact card): caption "BTC", then the price in headline, on one
// baseline; the headline's line box is centred in the 32px card.
static void drawBtcCard() {
  drawCard(TOK_LAYOUT_COL_LEFT_X, BTC_Y, TOK_LAYOUT_COL_LEFT_W, BTC_CARD_H);
  const int top = BTC_Y + (BTC_CARD_H - fontLineH(TOK_TYPE_HEADLINE)) / 2;
  const int baseline = top + fontAscent(TOK_TYPE_HEADLINE);
  int x = drawText(TOK_TYPE_CAPTION, SHINE_BAR_X, baseline - fontAscent(TOK_TYPE_CAPTION), "BTC",
                   TOK_COLOR_TEXT_SECONDARY);
  if (STATE.btcPrice >= 0)
    drawText(TOK_TYPE_NUMERAL_MD, x + TOK_SPACE_SM, top, fmtBtc(STATE.btcPrice), TOK_COLOR_TEXT_PRIMARY);
  else
    drawText(TOK_TYPE_NUMERAL_MD, x + TOK_SPACE_SM, top, "--", TOK_COLOR_TEXT_TERTIARY);
}

// ── NOTE PAGE (NOTE_PAGE) ──────────────────────────────────
// The right column as one card: "NOTE" label, then the text in type.mono.*
// from the first row below the corner slots.
static const int NOTE_X = TOK_LAYOUT_COL_RIGHT_X;
static const int NOTE_Y = TOK_LAYOUT_CONTENT_Y0;
static const int NOTE_W = TOK_LAYOUT_COL_RIGHT_W;
static const int NOTE_H = TOK_LAYOUT_CONTENT_Y1 - TOK_LAYOUT_CONTENT_Y0;
static const int NOTE_TX = NOTE_X + TOK_SPACE_CARD_PAD;            // 200, first glyph column
static const int NOTE_TY = NOTE_Y + TOK_SPACE_CARD_PAD + 17 + TOK_SPACE_SM;  // 45 (>= 40, clear of the corner)
// Exclusive bottom limit: a row is drawn only while y + lineH <= this.
static const int NOTE_TY_MAX = NOTE_Y + NOTE_H - TOK_SPACE_CARD_PAD;  // 268
// Usable width 260. Monospace advance per size (make_vlw.py):
//   size | font  | adv | step | cols | rows
//     1  | MONO1 |  7  |  15  |  37  |  14
//     2  | MONO2 |  10 |  21  |  26  |  10
//     3  | MONO3 |  13 |  29  |  20  |   7
// Every size holds at least the CYD pane's columns (24/12/8), so a note
// written against the CYD's note.html fit check wraps no worse here.
static const int NOTE_TEXT_W = NOTE_W - 2 * TOK_SPACE_CARD_PAD;
static const FontId NOTE_FONTS[3] = {TOK_TYPE_MONO_S, TOK_TYPE_MONO_M, TOK_TYPE_MONO_L};

// ── Syntax highlighting ───────────────────────────────────────────────────
// This rule set is duplicated in simulator-s3.html, and in ~/cyd's pages.cpp,
// simulator.html, note.html and note.py; all must agree or the board and the
// editor disagree about what the text looks like. Tokenizing happens at two
// levels -- source line, then word -- plus a one-character backtick toggle.
// There is deliberately no per-character classification. The colours are
// design.md's note.* tokens (section 11.19).
//
// Precedence, first match wins:
//   1. inside a `backtick span`      -> note.code (delimiters included)
//   2. word is TODO/FIXME/BUG        -> note.keyword.bad
//      word is DONE/OK               -> note.keyword.good
//      word is numeric               -> note.number
//   3. line begins '#'               -> note.heading (whole line)
//      line begins '>'               -> note.quote   (whole line)
//   4. leading "- ", "* ", "+ "      -> note.marker (the marker char only)
//   5. otherwise                     -> note.text
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
      noteRangeEquals(s, a, b, "BUG")) return TOK_NOTE_KEYWORD_BAD;
  if (noteRangeEquals(s, a, b, "DONE") || noteRangeEquals(s, a, b, "OK")) return TOK_NOTE_KEYWORD_GOOD;
  bool hasDigit = false;
  for (int i = a; i < b; i++) {
    if (!noteIsNumChar(s[i])) return 0;
    if (noteIsDigit(s[i])) hasDigit = true;
  }
  return hasDigit ? TOK_NOTE_NUMBER : 0;
}

// Word-wrapped, syntax-coloured render of STATE.note into the right-hand pane.
// Overflow past NOTE_TY_MAX is simply not drawn ("wrap + clip").
// Caller holds stateMutex; this must not re-lock.
static void drawNotePane() {
  drawCard(NOTE_X, NOTE_Y, NOTE_W, NOTE_H);
  drawSectionLabel(NOTE_TX, NOTE_Y + TOK_SPACE_CARD_PAD, "NOTE");

  const char* s = STATE.note;
  const int size = constrain(STATE.noteSize, 1, 3);
  const FontId font = NOTE_FONTS[size - 1];
  const int glyphW = FONT_ADV[font]['0' - 0x20];  // monospace: every advance is equal
  const int glyphH = fontLineH(font);
  const int lineH = glyphH;
  const int cols = NOTE_TEXT_W / glyphW;

  if (s[0] == '\0') {
    drawEmptyState(NOTE_X + NOTE_W / 2, NOTE_Y, NOTE_H, "No note yet", "Edit at :8787/note", false);
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
      if (s[p] == '#') lineColor = TOK_NOTE_HEADING;
      else if (s[p] == '>') lineColor = TOK_COLOR_TEXT_SECONDARY;  // note.quote
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
          c = TOK_NOTE_CODE;  // colour the tick itself, so a stray one is visible
        } else if (inCode) {
          c = TOK_NOTE_CODE;
        } else if (wordColor) {
          c = wordColor;
        } else if (lineColor) {
          c = lineColor;
        } else if (i == markerIdx) {
          c = TOK_NOTE_MARKER;
        } else {
          c = TOK_NOTE_TEXT;
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

// Page 6: the shared left column, with the note pane where the cats go.
static void drawNotePage() {
  drawLimitsColumn();
  drawBtcCard();
  drawNotePane();
}

// Mixed page's static half: left column + strip. The right pane belongs to
// the GIF player (gif_player.cpp cover-fits the cat there).
void drawMixedPageStatic() {
  g->fillRect(0, 0, MIXED_GIF_X0, SCREEN_H, TOK_COLOR_BG_CANVAS);
  g->fillRect(MIXED_GIF_X0, TOK_LAYOUT_CONTENT_Y1, SCREEN_W - MIXED_GIF_X0, SCREEN_H - TOK_LAYOUT_CONTENT_Y1,
              TOK_COLOR_BG_CANVAS);
  g->fillRect(MIXED_GIF_X0 + MIXED_GIF_W, 0, SCREEN_W - MIXED_GIF_X0 - MIXED_GIF_W, SCREEN_H, TOK_COLOR_BG_CANVAS);
  g->fillRect(MIXED_GIF_X0, 0, MIXED_GIF_W, MIXED_GIF_Y0, TOK_COLOR_BG_CANVAS);
  drawLimitsColumn();
  drawBtcCard();
  drawStatusStrip();
}

// ── ANALOG CLOCK (design.md 11.18) ─────────────────────────
// Filled pace wedge from the hour hand clockwise to the reset angle.
static void drawTimerWedge(int cx, int cy, int r, float startAngle, float endAngle) {
  float delta = fmodf(endAngle - startAngle, 360.0f);
  if (delta < 0) delta += 360.0f;
  g->fillArc(cx, cy, r, 0, startAngle, startAngle + delta, TOK_COLOR_DATA_PACE_WEDGE);
}

// Face (2px AA ring), 12 ticks, hour/minute hands, the pace wedge (Pace bars
// only), the green 5h-reset radius, the accent second hand, the hub. Angles
// are screen-space with -90deg so 0 points up.
static void drawAnalogClock(int cx, int cy, int r, int hour24, int minute, int second,
                             bool haveReset, int resetHour24, int resetMinute) {
  float hourAngle = ((hour24 % 12) + minute / 60.0f) * 30.0f - 90.0f;
  float minAngle = (minute + second / 60.0f) * 6.0f - 90.0f;
  float secAngle = second * 6.0f - 90.0f;
  float hourRad = hourAngle * PI / 180.0f;
  float minRad = minAngle * PI / 180.0f;
  float secRad = secAngle * PI / 180.0f;

  aaRing(cx, cy, r, 2, TOK_COLOR_TEXT_PRIMARY);
  if (haveReset && cfgShowCountdown) {
    float resetAngleWedge = ((resetHour24 % 12) + resetMinute / 60.0f) * 30.0f - 90.0f;
    drawTimerWedge(cx, cy, r - 2, hourAngle, resetAngleWedge);
  }
  for (int i = 0; i < 12; i++) {
    float tickRad = (i * 30.0f - 90.0f) * PI / 180.0f;
    int x0 = cx + (int)(cosf(tickRad) * (r - 2));
    int y0 = cy + (int)(sinf(tickRad) * (r - 2));
    int x1 = cx + (int)(cosf(tickRad) * (r - 8));
    int y1 = cy + (int)(sinf(tickRad) * (r - 8));
    g->drawWideLine(x0, y0, x1, y1, 1.0f, TOK_COLOR_TEXT_PRIMARY);
  }

  int hx = cx + (int)(cosf(hourRad) * r * 0.5f);
  int hy = cy + (int)(sinf(hourRad) * r * 0.5f);
  int mx = cx + (int)(cosf(minRad) * r * 0.8f);
  int my = cy + (int)(sinf(minRad) * r * 0.8f);
  int sx = cx + (int)(cosf(secRad) * (r - 3));
  int sy = cy + (int)(sinf(secRad) * (r - 3));

  g->drawWideLine(cx, cy, hx, hy, 3.0f, TOK_COLOR_TEXT_PRIMARY);
  g->drawWideLine(cx, cy, mx, my, 2.0f, TOK_COLOR_TEXT_PRIMARY);

  // Session (5h) reset time: a thin green radius, drawn before the second
  // hand so the sweeping hand stays on top.
  if (haveReset) {
    float resetAngle = ((resetHour24 % 12) + resetMinute / 60.0f) * 30.0f - 90.0f;
    float resetRad = resetAngle * PI / 180.0f;
    int rx = cx + (int)(cosf(resetRad) * (r - 3));
    int ry = cy + (int)(sinf(resetRad) * (r - 3));
    g->drawWideLine(cx, cy, rx, ry, 0.8f, TOK_COLOR_DATA_PACE);
  }

  g->drawWideLine(cx, cy, sx, sy, 1.2f, TOK_COLOR_ACCENT);
  aaFillCircle(cx, cy, 3, TOK_COLOR_TEXT_PRIMARY);
}

// ── AQI BADGE (design.md 11.16) ────────────────────────────
// Fill from the external EPA / aqicn.org scale; text gray.0, except white on
// Hazardous (both picked for contrast). Mirrored in simulator-s3.html.
static void aqiColors(int aqi, uint16_t& bg, uint16_t& fg) {
  fg = TOK_GRAY_0;
  if (aqi <= 50)       bg = TOK_AQI_GOOD;
  else if (aqi <= 100) bg = TOK_AQI_MODERATE;
  else if (aqi <= 150) bg = TOK_AQI_USG;
  else if (aqi <= 200) bg = TOK_AQI_UNHEALTHY;
  else if (aqi <= 300) bg = TOK_AQI_VERY_UNHEALTHY;
  else               { bg = TOK_AQI_HAZARDOUS; fg = TOK_GRAY_6; }
}

static int aqiBadgeW(int aqi) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", aqi);
  return textW(TOK_TYPE_HEADLINE, buf) + 2 * TOK_BADGE_PAD_X;
}
static void drawAqiBadge(int x, int y, int aqi) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", aqi);
  uint16_t bg, fg;
  aqiColors(aqi, bg, fg);
  aaFillRoundRect(x, y, aqiBadgeW(aqi), TOK_BADGE_H, TOK_RADIUS_SM, bg);
  drawText(TOK_TYPE_HEADLINE, x + TOK_BADGE_PAD_X, y + (TOK_BADGE_H - fontLineH(TOK_TYPE_HEADLINE)) / 2, buf, fg);
}

// ── STATUS PAGE (page 0, design.md 7.4) ────────────────────
// Left: 5H / WK limit cards + BTC. Right: the clock hero (analog r 76 beside
// the digital readout, date and AQI badge), then the tappable weather strip.
static const int CLOCK_R = 76;
static const int CLOCK_CX = TOK_LAYOUT_COL_RIGHT_X + TOK_SPACE_CARD_PAD_HERO + CLOCK_R;          // 272
static const int CLOCK_CY = CLOCK_CARD_Y + CLOCK_CARD_H / 2;                                     // 104
static const int READOUT_X = TOK_LAYOUT_COL_RIGHT_X + TOK_SPACE_CARD_PAD_HERO + 2 * CLOCK_R + 1 + TOK_SPACE_MD;  // 361

static void drawStatusPage() {
  drawLimitsColumn();
  drawBtcCard();
  drawCard(TOK_LAYOUT_COL_RIGHT_X, CLOCK_CARD_Y, TOK_LAYOUT_COL_RIGHT_W, CLOCK_CARD_H);
  const bool wxPressed = (pressedId == PRESS_WEATHER);
  const uint16_t wxBg = wxPressed ? TOK_COLOR_SURFACE_RAISED : TOK_COLOR_SURFACE_CARD;
  drawCardSurface(TOK_LAYOUT_COL_RIGHT_X, WEATHER_CARD_Y, TOK_LAYOUT_COL_RIGHT_W, WEATHER_CARD_H, wxBg);

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

  if (haveTime) {
    drawAnalogClock(CLOCK_CX, CLOCK_CY, CLOCK_R, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                    haveReset, resetHour, resetMinute);
  } else {
    aaRing(CLOCK_CX, CLOCK_CY, CLOCK_R, 2, TOK_COLOR_TEXT_TERTIARY);
  }

  // Readout column: time (numeral.lg), date (body), AQI badge -- 92px tall,
  // vertically centred on the card (a hero readout may centre, 7.2 rule 5).
  const bool haveAqi = cfgShowAqi && STATE.aqi >= 0;
  const int readH = fontLineH(TOK_TYPE_NUMERAL_LG) + TOK_SPACE_XS + fontLineH(TOK_TYPE_BODY) +
                    (haveAqi ? TOK_SPACE_SM + TOK_BADGE_H : 0);
  int y = CLOCK_CARD_Y + TOK_SPACE_CARD_PAD_HERO +
          (CLOCK_CARD_H - 2 * TOK_SPACE_CARD_PAD_HERO - readH) / 2;
  if (haveTime) {
    char hm[8];
    snprintf(hm, sizeof(hm), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    drawText(TOK_TYPE_NUMERAL_LG, READOUT_X, y, hm, TOK_COLOR_TEXT_PRIMARY);
  } else {
    drawText(TOK_TYPE_NUMERAL_LG, READOUT_X, y, "--:--", TOK_COLOR_TEXT_TERTIARY);
  }
  y += fontLineH(TOK_TYPE_NUMERAL_LG) + TOK_SPACE_XS;
  if (haveTime) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%s %d %s", WDAY_ABBR[timeinfo.tm_wday], timeinfo.tm_mday,
             MON_ABBR[timeinfo.tm_mon]);
    drawText(TOK_TYPE_BODY, READOUT_X, y, buf, TOK_COLOR_TEXT_SECONDARY);
  } else {
    drawText(TOK_TYPE_BODY, READOUT_X, y, "--", TOK_COLOR_TEXT_TERTIARY);
  }
  y += fontLineH(TOK_TYPE_BODY) + TOK_SPACE_SM;
  if (haveAqi) drawAqiBadge(READOUT_X, y, STATE.aqi);

  // ── weather strip (compact card, tappable -> Weather sheet) ──
  // H/L 20 | now 40 | 4 x 44 hourly | 24 disclosure column = 260 inner.
  const int ix = TOK_LAYOUT_COL_RIGHT_X + TOK_SPACE_CARD_PAD_COMPACT_H;   // 200
  const int iy = WEATHER_CARD_Y + TOK_SPACE_CARD_PAD_COMPACT_V;           // 216
  const int glyphCy = iy + 17 + 11;                                       // 244
  const int lowY = iy + 17 + 22;                                          // 255
  drawText(TOK_TYPE_NUMERAL_SM, ix, iy, STATE.weatherHigh > -900 ? String(STATE.weatherHigh) : String("--"),
           STATE.weatherHigh > -900 ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  drawText(TOK_TYPE_NUMERAL_SM, ix, lowY, STATE.weatherLow > -900 ? String(STATE.weatherLow) : String("--"),
           STATE.weatherLow > -900 ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_TERTIARY);

  // "Now": the current-item marker is accent.
  const int nowCx = ix + 20 + 20;
  drawTextC(TOK_TYPE_CAPTION, nowCx, iy, "Now", TOK_COLOR_ACCENT);
  drawWeatherIcon(nowCx, glyphCy, STATE.weatherCode, 1.2f);
  drawTempC(TOK_TYPE_NUMERAL_SM, nowCx, lowY, (int)round(STATE.weatherTempC), STATE.weatherTempC > -900);

  // Next 4 hours: weatherHourly[] starts at the current hour (the "now"
  // column), so indices 1..4.
  for (int i = 0; i < 4; i++) {
    int idx = i + 1;
    int cx = ix + 60 + i * 44 + 22;
    bool have = STATE.weatherHourlyCount > idx;
    char hbuf[4];
    if (have) snprintf(hbuf, sizeof(hbuf), "%02d", STATE.weatherHourly[idx].hour);
    drawTextC(TOK_TYPE_NUMERAL_SM, cx, iy, have ? hbuf : "--",
              have ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_TERTIARY);
    drawWeatherIcon(cx, glyphCy, have ? STATE.weatherHourly[idx].code : -1, 1.2f);
    drawTempC(TOK_TYPE_NUMERAL_SM, cx, lowY, have ? STATE.weatherHourly[idx].tempC : 0, have);
  }
  // Disclosure chevron: a hint, always tertiary; the whole card is the target.
  drawChevron(TOK_LAYOUT_COL_RIGHT_X + TOK_LAYOUT_COL_RIGHT_W - TOK_SPACE_CARD_PAD_COMPACT_H - 8,
              WEATHER_CARD_Y + WEATHER_CARD_H / 2, TOK_COLOR_TEXT_TERTIARY);
}

// ── DEVICE STATS SHEET ─────────────────────────────────────
// Modal header ("Device stats"), then one card of five data rows: label,
// detail (caption) + percent (headline), a data.system meter (status.error
// at >= 80%, with the % beside it so colour is never the only cue).
static void drawDeviceRow(int y, const char* label, const String& detail, int percent) {
  const int x = TOK_LAYOUT_CONTENT_X0 + TOK_SPACE_CARD_PAD, r = TOK_LAYOUT_CONTENT_X1 - TOK_SPACE_CARD_PAD;
  drawText(TOK_TYPE_BODY, x, y, label, TOK_COLOR_TEXT_PRIMARY);
  String pct = percent >= 0 ? String(percent) + "%" : String("--");
  drawTextR(TOK_TYPE_NUMERAL_MD, r, y, pct, percent >= 0 ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  int dx = r - textW(TOK_TYPE_NUMERAL_MD, pct) - TOK_SPACE_SM;
  if (detail.length())
    drawTextR(TOK_TYPE_CAPTION, dx, y + fontAscent(TOK_TYPE_BODY) - fontAscent(TOK_TYPE_CAPTION), detail,
              TOK_COLOR_TEXT_SECONDARY);
  drawMeter(x, y + 23 + TOK_SPACE_STACK_TIGHT, r - x, TOK_METER_MD, percent,
            percent >= 80 ? TOK_COLOR_STATUS_ERROR : TOK_COLOR_DATA_SYSTEM);
}

static void drawDevicePage() {
  drawModalHeader(false, "Device stats", pressedId == PRESS_CLOSE);
  const int cardY = TOK_LAYOUT_HEADER_CONTENT_Y;
  drawCard(TOK_LAYOUT_CONTENT_X0, cardY, TOK_LAYOUT_CONTENT_W, 5 * ROW_H + 4 * TOK_SPACE_STACK + 2 * TOK_SPACE_CARD_PAD);
  int y = cardY + TOK_SPACE_CARD_PAD;

  int cpuInt = (int)(cpuPercentAvg + 0.5f);
  drawDeviceRow(y, "CPU", "Render loop duty cycle", cpuInt);
  y += ROW_STEP;
  uint32_t flashUsed, flashTotal;
  int flashPct = flashPercent(flashUsed, flashTotal);
  drawDeviceRow(y, "Flash (app partition)", fmtKB(flashUsed) + " / " + fmtKB(flashTotal), flashPct);
  y += ROW_STEP;
  uint32_t ramUsed, ramTotal;
  int ramPct = staticRamPercent(ramUsed, ramTotal);
  drawDeviceRow(y, "Internal RAM", fmtKB(ramUsed) + " / " + fmtKB(ramTotal), ramPct);
  y += ROW_STEP;
  uint32_t psUsed, psTotal;
  int psPct = psramPercent(psUsed, psTotal);
  drawDeviceRow(y, "PSRAM", fmtKB(psUsed) + " / " + fmtKB(psTotal), psPct);
  y += ROW_STEP;
  uint64_t sdUsed = 0, sdTotal = 0;
  int sdPct = cachedSdCapacityPercent(sdUsed, sdTotal);
  drawDeviceRow(y, "SD card", sdPct >= 0 ? fmtGB(sdUsed) + " / " + fmtGB(sdTotal) : String("Not found"), sdPct);
}

// ── WEATHER SHEET ──────────────────────────────────────────
// The hero card takes the header band in place of a title (design.md 7.3):
// x 60..471, y 8..71. Then the hourly card (next 6) and the 5-day card.
static const int WX_HERO_X = TOK_HEADER_TITLE_X, WX_HERO_Y = TOK_LAYOUT_CONTENT_Y0;
static const int WX_HERO_W = TOK_LAYOUT_CONTENT_X1 - WX_HERO_X, WX_HERO_H = 64;
static const int WX_HOURLY_Y = WX_HERO_Y + WX_HERO_H + TOK_SPACE_GUTTER;          // 80
static const int WX_HOURLY_H = 17 + TOK_SPACE_XS + 28 + TOK_SPACE_XS + 23 + 2 * TOK_SPACE_CARD_PAD_COMPACT_V;  // 92
static const int WX_DAILY_Y = WX_HOURLY_Y + WX_HOURLY_H + TOK_SPACE_GUTTER;       // 180
static const int WX_DAILY_H = TOK_LAYOUT_OVERLAY_CONTENT_Y1 - WX_DAILY_Y;          // 132

static void drawWeatherPage() {
  drawModalHeader(false, nullptr, pressedId == PRESS_CLOSE);

  // ── Hero: glyph.content.lg, the display temperature + degree ring,
  //    condition (headline) over H / L (body), AQI badge right ──
  drawCard(WX_HERO_X, WX_HERO_Y, WX_HERO_W, WX_HERO_H);
  const int hx = WX_HERO_X + TOK_SPACE_CARD_PAD_HERO;
  drawWeatherIcon(hx + 18, WX_HERO_Y + WX_HERO_H / 2, STATE.weatherCode, 2.0f);
  const int tempY = WX_HERO_Y + (WX_HERO_H - fontLineH(TOK_TYPE_DISPLAY)) / 2;
  const int tempX = hx + 36 + TOK_SPACE_SM;
  int metaX;
  if (STATE.weatherTempC > -900) {
    char tbuf[6];
    snprintf(tbuf, sizeof(tbuf), "%d", (int)round(STATE.weatherTempC));
    int x = drawText(TOK_TYPE_NUMERAL_HERO, tempX, tempY, tbuf, TOK_COLOR_TEXT_PRIMARY);
    int capTop = tempY + fontAscent(TOK_TYPE_DISPLAY) - 29;  // capH ~ 0.73 x 40
    metaX = drawDegreeRing(x + TOK_SPACE_HAIR, capTop, TOK_COLOR_SURFACE_CARD) + TOK_SPACE_LG;
  } else {
    metaX = drawText(TOK_TYPE_NUMERAL_HERO, tempX, tempY, "--", TOK_COLOR_TEXT_TERTIARY) + TOK_SPACE_LG;
  }

  // AQI badge right, 8px clear of corner slot b (reserved even while hidden).
  const bool haveAqi = cfgShowAqi && STATE.aqi >= 0;
  const int badgeW = haveAqi ? aqiBadgeW(STATE.aqi) : 0;
  const int badgeX = TOK_CORNER_INK_X1 - badgeW;
  const int metaMaxX = (haveAqi ? badgeX - TOK_SPACE_SM : TOK_CORNER_INK_X1);
  const int metaY = WX_HERO_Y + (WX_HERO_H - (23 + TOK_SPACE_XS + 23)) / 2;
  {
    const char* cond = STATE.weatherCondition[0] ? STATE.weatherCondition : "--";
    drawText(TOK_TYPE_HEADLINE, metaX, metaY, fitText(TOK_TYPE_HEADLINE, cond, metaMaxX - metaX),
             STATE.weatherCondition[0] ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  }
  {
    const int y = metaY + 23 + TOK_SPACE_XS;
    int x = drawText(TOK_TYPE_BODY, metaX, y, "H ", TOK_COLOR_TEXT_SECONDARY);
    if (STATE.weatherHigh > -900) x = drawText(TOK_TYPE_BODY, x, y, String(STATE.weatherHigh), TOK_COLOR_TEXT_PRIMARY);
    else x = drawText(TOK_TYPE_BODY, x, y, "--", TOK_COLOR_TEXT_TERTIARY);
    x = drawText(TOK_TYPE_BODY, x + TOK_SPACE_LG, y, "L ", TOK_COLOR_TEXT_SECONDARY);
    if (STATE.weatherLow > -900) drawText(TOK_TYPE_BODY, x, y, String(STATE.weatherLow), TOK_COLOR_TEXT_PRIMARY);
    else drawText(TOK_TYPE_BODY, x, y, "--", TOK_COLOR_TEXT_TERTIARY);
  }
  if (haveAqi) drawAqiBadge(badgeX, WX_HERO_Y + (WX_HERO_H - TOK_BADGE_H) / 2, STATE.aqi);

  // ── Hourly (next 6): hour (caption), glyph.content.md, temperature (body) ──
  drawCard(TOK_LAYOUT_CONTENT_X0, WX_HOURLY_Y, TOK_LAYOUT_CONTENT_W, WX_HOURLY_H);
  const int innerX = TOK_LAYOUT_CONTENT_X0 + TOK_SPACE_CARD_PAD_COMPACT_H;
  const int innerW = TOK_LAYOUT_CONTENT_W - 2 * TOK_SPACE_CARD_PAD_COMPACT_H;
  const int slotW = innerW / WEATHER_HOURLY_N;
  const int slotX0 = innerX + (innerW - slotW * WEATHER_HOURLY_N) / 2;
  const int hy = WX_HOURLY_Y + TOK_SPACE_CARD_PAD_COMPACT_V;
  for (int i = 0; i < WEATHER_HOURLY_N; i++) {
    int cx = slotX0 + i * slotW + slotW / 2;
    bool have = (i < (int)STATE.weatherHourlyCount);
    int hour = have ? STATE.weatherHourly[i].hour : -1;
    char hbuf[4];
    if (have && hour >= 0) snprintf(hbuf, sizeof(hbuf), "%02d", hour);
    // First slot = the current hour: the current-item marker (accent).
    drawTextC(TOK_TYPE_NUMERAL_SM, cx, hy, (have && hour >= 0) ? hbuf : "--",
              i == 0 ? TOK_COLOR_ACCENT : (have ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_TERTIARY));
    drawWeatherIcon(cx, hy + 17 + TOK_SPACE_XS + 14, have ? STATE.weatherHourly[i].code : -1, 1.5f);
    const int ty = hy + 17 + TOK_SPACE_XS + 28 + TOK_SPACE_XS;
    if (have) drawTextC(TOK_TYPE_BODY, cx, ty, String(STATE.weatherHourly[i].tempC), TOK_COLOR_TEXT_PRIMARY);
    else drawTextC(TOK_TYPE_BODY, cx, ty, "--", TOK_COLOR_TEXT_TERTIARY);
  }

  // ── 5-day: day | glyph | low | range meter | high, rows one line box apart ──
  drawCard(TOK_LAYOUT_CONTENT_X0, WX_DAILY_Y, TOK_LAYOUT_CONTENT_W, WX_DAILY_H);
  int minT = 100, maxT = -100;
  for (uint8_t i = 0; i < STATE.weatherDailyCount; i++) {
    if (STATE.weatherDaily[i].low < minT) minT = STATE.weatherDaily[i].low;
    if (STATE.weatherDaily[i].high > maxT) maxT = STATE.weatherDaily[i].high;
  }
  if (maxT <= minT) { minT = 20; maxT = 40; }
  const int dayX = innerX, iconCx = innerX + 64, lowR = innerX + 120;
  const int barX = TOK_LAYOUT_COL_RIGHT_X - 36, barW = 260, highX = barX + barW + TOK_SPACE_SM;
  const int rowH = fontLineH(TOK_TYPE_BODY);
  const int dy0 = WX_DAILY_Y + TOK_SPACE_CARD_PAD_COMPACT_V;
  for (int i = 0; i < WEATHER_DAILY_N; i++) {
    int y = dy0 + i * rowH;
    bool have = (i < (int)STATE.weatherDailyCount);
    int wd = have ? STATE.weatherDaily[i].wday : -1;
    int lo = have ? STATE.weatherDaily[i].low : 0;
    int hi = have ? STATE.weatherDaily[i].high : 0;
    bool haveWd = have && wd >= 0 && wd < 7;
    drawText(TOK_TYPE_BODY, dayX, y, haveWd ? WDAY_ABBR[wd] : "--",
             !haveWd ? TOK_COLOR_TEXT_TERTIARY : (i == 0 ? TOK_COLOR_ACCENT : TOK_COLOR_TEXT_PRIMARY));
    drawWeatherIcon(iconCx, y + rowH / 2, have ? STATE.weatherDaily[i].code : -1, 1.0f);
    if (have) drawTextR(TOK_TYPE_BODY, lowR, y, String(lo), TOK_COLOR_TEXT_SECONDARY);
    else drawTextR(TOK_TYPE_BODY, lowR, y, "--", TOK_COLOR_TEXT_TERTIARY);
    const int barY = y + 12 - TOK_METER_MD / 2;
    aaFillRoundRect(barX, barY, barW, TOK_METER_MD, TOK_METER_MD / 2, TOK_COLOR_FILL_TRACK);
    if (have) {
      float span = (float)(maxT - minT);
      int x0 = barX + (int)((lo - minT) / span * barW);
      int x1 = barX + (int)((hi - minT) / span * barW);
      if (x1 - x0 < TOK_METER_MD) x1 = x0 + TOK_METER_MD;
      aaFillRoundRect(x0, barY, x1 - x0, TOK_METER_MD, TOK_METER_MD / 2, TOK_COLOR_TEXT_PRIMARY);
    }
    if (have) drawText(TOK_TYPE_BODY, highX, y, String(hi), TOK_COLOR_TEXT_PRIMARY);
    else drawText(TOK_TYPE_BODY, highX, y, "--", TOK_COLOR_TEXT_TERTIARY);
  }
}

// ── RENDER ─────────────────────────────────────────────────
void render() {
  // The cat pages are animated frame-by-frame by gifTick() in loop(), not
  // drawn here (unless a sheet is up over them); offline is also gifTick()'s.
  bool sheet = weatherPageOpen || devicePageOpen;
  if (!sheet && (currentPage == GIF_PAGE || currentPage == MIXED_PAGE)) return;
  uint32_t startUs = micros();
  // Hold the lock across all STATE reads, release before the present.
  lockState();
  g->fillScreen(TOK_COLOR_BG_CANVAS);
  if (weatherPageOpen) {
    drawWeatherPage();
  } else if (devicePageOpen) {
    drawDevicePage();
  } else {
    switch (currentPage) {
      case 0: drawStatusPage(); break;
      case 1: drawProjectsPage(); break;  // top projects + 7-day trend
      case 2: drawLimitsPage(); break;    // /usage-style limits panel
      case 5: drawNotePage(); break;      // left column + note pane
    }
    drawStatusStrip();
  }
  drawSystemCorner(false);
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

// ── MEDIA OVERLAYS (gif_player.cpp) ────────────────────────
// Reset readout on the full-screen cat page and the offline screen: the
// usage % (data.usage) + "Resets 03:19" (primary), headline -- one weight up
// from body, as text over media must be -- on a plate, bottom-left.
void drawResetPlate() {
  lockState();
  int percent = STATE.sessionPercent;
  String resets = STATE.sessionResets;
  unlockState();
  if (percent < 0 || !resets.length()) return;
  String a = String(percent) + "%", b = "Resets " + resets;
  const int padX = TOK_SPACE_SM, padY = 6;
  const int gap = TOK_SPACE_SM;
  int w = textW(TOK_TYPE_HEADLINE, a) + gap + textW(TOK_TYPE_HEADLINE, b) + 2 * padX;
  int h = fontLineH(TOK_TYPE_HEADLINE) + 2 * padY;
  int x = TOK_SPACE_SM, y = SCREEN_H - TOK_SPACE_SM - h;
  aaFillRoundRect(x, y, w, h, TOK_RADIUS_SM, TOK_COLOR_PLATE);
  int tx = drawText(TOK_TYPE_HEADLINE, x + padX, y + padY, a, TOK_COLOR_DATA_USAGE);
  drawText(TOK_TYPE_HEADLINE, tx + gap, y + padY, b, TOK_COLOR_TEXT_PRIMARY);
}
