// Shared drawing for the pages and sheets: AA fills, primitives, glyphs, corner, weather glyphs, pace sweep, left column + BTC, clocks, month grid.
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"

const char* const WDAY_ABBR[7] = {
  "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
const char* const WDAY_FULL[7] = {
  "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
};
const char* const MON_ABBR[12] = {
  "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// ── ANTI-ALIASED FILLS ─────────────────────────────────────
// LovyanGFX's fillSmoothRoundRect algorithm (LGFXBase.inl: solid spans + 1px
// alpha edge pixels, 1/32 thresholds) with one change: edge pixels blend
// straight into the sprite's big-endian RGB565 buffer instead of going
// through the library's per-pixel readRect/writeImage effect path, which cost
// ~30 ms on the status page. The pixels are the same algorithm's; the
// simulator's gfx.fillSmoothRoundRect is the twin. Respects the clip rect.
// aaTarget is the sprite `g` draws into: `frame`, or the weather icon mask.
static LGFX_Sprite* aaTarget = &frame;
static inline void blendPx(int x, int y, uint16_t c, uint8_t a) {
  int32_t cx, cy, cw, ch;
  g->getClipRect(&cx, &cy, &cw, &ch);
  if (x < cx || y < cy || x >= cx + cw || y >= cy + ch) return;
  uint16_t* p = (uint16_t*)aaTarget->getBuffer() + y * aaTarget->width() + x;
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

// The inverse of a round-rect fill, for media already in the frame (cat GIFs,
// movies): paint `bg` outside a radius-r arc in each corner of (x, y, w, h),
// blending the 1px anti-aliased edge over the media. Edge pixels blend, so
// running it twice over the same pixels darkens them: callers clip it to the
// pixels they just rewrote. Respects the clip rect.
void aaMaskRoundCorners(int x, int y, int w, int h, int r, uint16_t bg) {
  if (r > w / 2) r = w / 2;
  if (r > h / 2) r = h / 2;
  const float LO = 1.0f / 32.0f, HI = 1.0f - LO;
  for (int j = 0; j < r; j++) {
    for (int i = 0; i < r; i++) {
      float dx = r - i - 0.5f, dy = r - j - 0.5f;
      float a = sqrtf(dx * dx + dy * dy) - r + 0.5f;  // bg coverage of this pixel
      if (a < LO) break;  // the rest of this row is inside the arc
      const int px[4] = {x + i, x + w - 1 - i, x + i, x + w - 1 - i};
      const int py[4] = {y + j, y + j, y + h - 1 - j, y + h - 1 - j};
      for (int k = 0; k < 4; k++) {
        if (a > HI) g->drawPixel(px[k], py[k], bg);
        else blendPx(px[k], py[k], bg, (uint8_t)(a * 255));
      }
    }
  }
}

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

// Sub-pixel sibling of aaRing for motion: edges at float distances ro > ri
// from the pixel centre (ri <= 0 = a solid disc), coverage scaled by alpha.
// A radius that moves a fraction of a pixel per frame reads as smooth growth
// instead of integer steps.
void aaRingF(int cx, int cy, float ro, float ri, uint16_t c, uint8_t alpha) {
  const int rMax = (int)ceilf(ro);
  for (int dy = -rMax; dy <= rMax; dy++) {
    for (int dx = 0; dx <= rMax; dx++) {
      float d = sqrtf((float)(dx * dx + dy * dy));
      float cov = (constrain(ro - d, 0.0f, 1.0f) - constrain(ri - d, 0.0f, 1.0f)) * alpha / 255.0f;
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
void drawCard(int x, int y, int w, int h) { drawCardSurface(x, y, w, h, TOK_COLOR_SURFACE_CARD); }

// Section label (design.md 11.2): type.label, UPPERCASE, text.secondary.
void drawSectionLabel(int x, int y, const char* label) {
  drawText(TOK_TYPE_LABEL, x, y, label, TOK_COLOR_TEXT_SECONDARY);
}

// Meter (design.md 11.4): fill.track + a data-series fill, radius.full; a
// non-zero value is at least a full-height stub; unknown = the track alone.
// Returns the fill width, or -1 when unknown.
int drawMeter(int x, int y, int w, int h, int percent, uint16_t color) {
  aaFillRoundRect(x, y, w, h, h / 2, TOK_COLOR_FILL_TRACK);
  if (percent < 0) return -1;
  int fillW = (int)((float)min(percent, 100) / 100 * w + 0.5f);
  if (fillW < h) fillW = h;
  aaFillRoundRect(x, y, fillW, h, h / 2, color);
  return fillW;
}

// Text truncated on MEASURED width (design.md 5.3 rule 6): cut at a word
// boundary if one is within 4 characters of the cut, else hard.
String fitText(FontId f, const char* s, int maxW) {
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
void drawGearGlyph(int cx, int cy, uint16_t c, uint16_t bg) {
  aaFillCircle(cx, cy, 6, c);   // ring r 5, 2px
  aaFillCircle(cx, cy, 4, bg);
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    g->drawWideLine(cx + lroundf(cosf(a) * 7), cy + lroundf(sinf(a) * 7),
                    cx + lroundf(cosf(a) * 9), cy + lroundf(sinf(a) * 9), TOK_STROKE_GLYPH_R, c);
  }
}
// Icon button pressed state (design.md 11.10): only the 40px fill.pressed disc
// behind the glyph -- the caller draws the glyph (primary) on top. Used by the
// modal header's close/back; the sleep pill and shuffle disc press differently.
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

void shuffleCentre(bool offline, int& cx, int& cy) {
  // 8px in from the media box's bottom-right: the full screen, or the pane.
  int x1 = SCREEN_W, y1 = SCREEN_H;
  if (catPanePage(offline)) {
    int px, py, pw, ph;
    catPaneRect(px, py, pw, ph);
    x1 = px + pw;
    y1 = py + ph;
  }
  cx = x1 - TOK_SPACE_SM - SHUFFLE_DISC_R - 1;
  cy = y1 - TOK_SPACE_SM - SHUFFLE_DISC_R - 1;
}

// ── SYSTEM CORNER (design.md 7.3) ──────────────────────────
// Slot a: the sleep icon button (a bare minimal grey pill, no disc or plate;
// pressed = the pill steps to primary). Slot b: the Battery Save glyph, only
// while active; over media it sits on a plate with a 4px inset around its
// glyph box. Drawn last on every screen.
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
  const int cx = TOK_CORNER_SLOT_A_X + sz / 2, cy = ay + sz / 2;
  const bool pressed = (pressedId == PRESS_SLEEP);
  aaFillRoundRect(cx - 7, cy - 3, 14, 6, 3, pressed ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_SECONDARY);
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


// ── DATA HELPERS ───────────────────────────────────────────
// Stale data (state.h). One missed poll is not news, so the bar is two
// intervals, floored at a minute; offline mode (main.cpp's offlineAfterMs)
// takes over at 3 min / 3 polls.
bool dataStale() {
  return dataStaleAt(STATE.haveData, lastFetchSuccessMs, POLL_INTERVAL_MS, millis());
}

// Live countdown: tick the last server-provided remaining-seconds down by
// wall time since lastFetchOkMs.
long liveResetsInSec(long baseSec) {
  return liveResetsFrom(baseSec, millis(), STATE.lastFetchOkMs);
}


// ── WEATHER GLYPHS (content family, design.md 10) ──────────
// Filled vector shapes mapped from Open-Meteo's WMO weather_code, CENTRED on
// (cx, cy) and scaled by k from the CYD's ~18px geometry. Each is one colour.
uint16_t weatherIconColour(int code) {
  if (code <= 1 || code >= 95) return TOK_COLOR_CONTENT_SUN;  // clear, thunderstorm
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) return TOK_COLOR_CONTENT_SNOW;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return TOK_COLOR_CONTENT_RAIN;
  return TOK_COLOR_CONTENT_CLOUD;
}

// The shape alone, every part in ink `c`.
void weatherIconShape(int cx, int cy, int code, float k, uint16_t c) {
  auto S = [k](float v) { return (int)lroundf(v * k); };
  float lw = 1.0f * k;
  if (code == 0 || code == 1) {
    // clear: sun disc + 8 short rounded rays with a gap between disc and rays.
    aaFillCircle(cx, cy, S(4), c);
    g->drawWideLine(cx, cy - S(9), cx, cy - S(7), lw, c);
    g->drawWideLine(cx, cy + S(7), cx, cy + S(9), lw, c);
    g->drawWideLine(cx - S(9), cy, cx - S(7), cy, lw, c);
    g->drawWideLine(cx + S(7), cy, cx + S(9), cy, lw, c);
    g->drawWideLine(cx - S(7), cy - S(7), cx - S(5), cy - S(5), lw, c);
    g->drawWideLine(cx + S(5), cy + S(5), cx + S(7), cy + S(7), lw, c);
    g->drawWideLine(cx - S(7), cy + S(7), cx - S(5), cy + S(5), lw, c);
    g->drawWideLine(cx + S(5), cy - S(5), cx + S(7), cy - S(7), lw, c);
    return;
  }
  // Rain/snow/lightning are drawn standalone (no cloud underneath) so the
  // condition itself reads clearly at small sizes.
  if (code >= 95) {
    // thunderstorm: zigzag bolt as 4 triangles. Vertices:
    // A(-1,+8) B(-1,+2) C(-5,+2) D(+1,-8) E(+1,-2) F(+5,-2).
    g->fillTriangle(cx - S(5), cy + S(2), cx + S(1), cy - S(8), cx + S(1), cy - S(2), c);
    g->fillTriangle(cx - S(5), cy + S(2), cx + S(1), cy - S(2), cx - S(1), cy + S(2), c);
    g->fillTriangle(cx - S(1), cy + S(2), cx + S(1), cy - S(2), cx + S(5), cy - S(2), c);
    g->fillTriangle(cx - S(1), cy + S(2), cx + S(5), cy - S(2), cx - S(1), cy + S(8), c);
    return;
  }
  if ((code >= 71 && code <= 77) || code == 85 || code == 86) {
    // snow: six-armed snowflake = three rounded lines crossing at 60deg
    g->drawWideLine(cx, cy - S(7), cx, cy + S(7), lw, c);
    g->drawWideLine(cx - S(6), cy - S(4), cx + S(6), cy + S(4), lw, c);
    g->drawWideLine(cx - S(6), cy + S(4), cx + S(6), cy - S(4), lw, c);
    return;
  }
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    // rain: one centred teardrop, tip at y -8, a r 5 bulb centred at y +3;
    // the triangle meets the bulb on its tangents (x +-4.45, y +0.7).
    g->fillTriangle(cx, cy - S(8), cx - S(4.45f), cy + S(0.7f), cx + S(4.45f), cy + S(0.7f), c);
    aaFillCircle(cx, cy + S(3), S(5), c);
    return;
  }
  // Everything else shares a plain cloud (2/3/45/48 = cloudy/fog, or any
  // unmapped code): two overlapping puffs on a fully-rounded pill base.
  aaFillCircle(cx - S(4), cy - S(2), S(4), c);
  aaFillCircle(cx + S(3), cy - S(3), S(5), c);
  aaFillRoundRect(cx - S(9), cy - S(2), S(19), S(9), S(4), c);
}


// ── Weather glyph material (design.md 10.5): drop shadow, vertical gradient,
//    emboss bevel, specular spot. The shape renders white-on-black into a
//    64x64 coverage mask; every effect is computed from that mask and
//    composited over the frame, so shape and base colour stay exactly the
//    shape's. Integer math, twin of simulator-s3.html's compositeWeatherIcon.
static const int WX_MASK = 64, WX_MASK_C = WX_MASK / 2;
static LGFX_Sprite wxMask;
static uint8_t wxCov[WX_MASK * WX_MASK];
static int wxX0, wxY0, wxX1, wxY1;  // coverage bbox in the mask, inclusive

static inline int covAt(int x, int y) {
  return (x < 0 || y < 0 || x >= WX_MASK || y >= WX_MASK) ? 0 : wxCov[y * WX_MASK + x];
}
static inline int lighten(int v, int amt) { return v + (((255 - v) * amt) >> 8); }  // amt 0..256
static inline int darken(int v, int amt) { return v - ((v * amt) >> 8); }

// Base colour shaded for mask pixel (mx, my); r9 = the glyph's 9-unit radius.
void wxShade(int mx, int my, int r9, int& r, int& gg, int& b) {
  int ch[3] = {r, gg, b};
  int t = (my - WX_MASK_C) * 256 / r9;                      // -256 top .. 256 bottom
  if (t > 256) t = 256;
  if (t < -256) t = -256;
  int e = covAt(mx - 1, my - 1) - covAt(mx + 1, my + 1);    // emboss, light from top-left
  int u = (mx - WX_MASK_C) * 256 / r9 + 90, v = (my - WX_MASK_C) * 256 / r9 + 115;
  int spot = u * u / 52 + v * v / 23;                        // < 256 inside the specular ellipse
  for (int i = 0; i < 3; i++) {
    int c = ch[i];
    c = t < 0 ? lighten(c, (-t * 56) >> 8) : darken(c, (t * 64) >> 8);   // gradient
    c = e < 0 ? lighten(c, (-e * 110) >> 8) : darken(c, (e * 90) >> 8);  // bevel
    if (spot < 256) c = lighten(c, ((256 - spot) * 130) >> 8);            // reflection
    ch[i] = c;
  }
  r = ch[0]; gg = ch[1]; b = ch[2];
}

void compositeWeatherIcon(int cx, int cy, float k, uint16_t base) {
  const int r9 = (int)lroundf(9 * k);
  const int sdx = max(1, (int)lroundf(0.6f * k)), sdy = max(1, (int)lroundf(1.2f * k));
  int br = (base >> 11) & 31, bg = (base >> 5) & 63, bb = base & 31;
  br = (br << 3) | (br >> 2); bg = (bg << 2) | (bg >> 4); bb = (bb << 3) | (bb >> 2);
  uint16_t* fb = (uint16_t*)frame.getBuffer();
  const int ox = cx - WX_MASK_C, oy = cy - WX_MASK_C;
  // Only the coverage bbox, grown by the shadow's reach (offset + blur).
  const int x0 = max(0, wxX0 - 1), y0 = max(0, wxY0 - 1);
  const int x1 = min(WX_MASK - 1, wxX1 + sdx + 1), y1 = min(WX_MASK - 1, wxY1 + sdy + 1);
  for (int my = y0; my <= y1; my++) {
    int fy = oy + my;
    if (fy < 0 || fy >= SCREEN_H) continue;
    for (int mx = x0; mx <= x1; mx++) {
      int fx = ox + mx;
      if (fx < 0 || fx >= SCREEN_W) continue;
      int cov = wxCov[my * WX_MASK + mx];
      int sh = 0;  // 3x3 box-blurred coverage under the offset shadow
      for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++) sh += covAt(mx - sdx + i, my - sdy + j);
      sh /= 9;
      if (!cov && !sh) continue;
      uint16_t* p = fb + fy * SCREEN_W + fx;
      uint16_t d = (uint16_t)((*p >> 8) | (*p << 8));
      int dr = (d >> 11) & 31, dg = (d >> 5) & 63, db = d & 31;
      dr = (dr << 3) | (dr >> 2); dg = (dg << 2) | (dg >> 4); db = (db << 3) | (db >> 2);
      if (sh) {
        int a = (sh * 140) >> 8;  // shadow.opacity ~55%
        dr = darken(dr, a); dg = darken(dg, a); db = darken(db, a);
      }
      if (cov) {
        int sr = br, sg = bg, sb = bb;
        wxShade(mx, my, r9, sr, sg, sb);
        dr += (sr - dr) * cov / 255; dg += (sg - dg) * cov / 255; db += (sb - db) * cov / 255;
      }
      uint16_t o = (uint16_t)(((dr >> 3) << 11) | ((dg >> 2) << 5) | (db >> 3));
      *p = (uint16_t)((o >> 8) | (o << 8));
    }
  }
}

void drawWeatherIcon(int cx, int cy, int code, float k) {
  if (code < 0) {
    drawTextC(TOK_TYPE_CAPTION, cx, cy - fontLineH(TOK_TYPE_CAPTION) / 2, "--", TOK_COLOR_TEXT_TERTIARY);
    return;
  }
  if (!wxMask.getBuffer()) {
    wxMask.setColorDepth(16);
    wxMask.setPsram(false);
    if (!wxMask.createSprite(WX_MASK, WX_MASK)) {  // no mask: the flat glyph
      weatherIconShape(cx, cy, code, k, weatherIconColour(code));
      return;
    }
  }
  lgfx::LovyanGFX* saved = g;
  wxMask.fillSprite(TFT_BLACK);
  g = &wxMask; aaTarget = &wxMask;
  weatherIconShape(WX_MASK_C, WX_MASK_C, code, k, TFT_WHITE);
  g = saved; aaTarget = &frame;
  const uint16_t* m = (const uint16_t*)wxMask.getBuffer();
  wxX0 = wxY0 = WX_MASK; wxX1 = wxY1 = -1;
  for (int i = 0; i < WX_MASK * WX_MASK; i++) {
    int g6 = (((uint16_t)((m[i] >> 8) | (m[i] << 8))) >> 5) & 63;
    wxCov[i] = (uint8_t)((g6 << 2) | (g6 >> 4));
    if (g6) {
      int x = i % WX_MASK, y = i / WX_MASK;
      wxX0 = min(wxX0, x); wxX1 = max(wxX1, x); wxY0 = min(wxY0, y); wxY1 = max(wxY1, y);
    }
  }
  if (wxX1 < 0) return;
  compositeWeatherIcon(cx, cy, k, weatherIconColour(code));
}

// Degree ring (design.md 10.3): r 4 / r 3, text.secondary, top-aligned to
// the cap height of the number it follows. Returns the x past it.
int drawDegreeRing(int x, int capTop, uint16_t bg) {
  aaFillCircle(x + 4, capTop + 4, 4, TOK_COLOR_TEXT_SECONDARY);
  aaFillCircle(x + 4, capTop + 4, 2, bg);
  return x + 9;
}

// Compact temperature: the value, then "C" in caption + secondary on the
// same baseline (design.md 5.3 rule 4). Centred on cx.
void drawTemp(FontId f, int cx, int y, int t, bool have) {
  if (have) drawTextC(f, cx, y, String(t), TOK_COLOR_TEXT_PRIMARY);
  else drawTextC(f, cx, y, "--", TOK_COLOR_TEXT_TERTIARY);
}

// ── PACE SWEEP (motion.sweep) ──────────────────────────────
// One light band every 3s across the green pace fills of the limit cards
// (the shared left column of the status, mixed and note pages, and the
// Overview's 5H / WK cells), then still: it means "fresh data". Constant
// speed, 600 ms. Twin of simulator-s3.html's.
static const int SHINE_BAND_R = 10;
static const int SHINE_BAR_X = TOK_LAYOUT_COL_LEFT_X + TOK_SPACE_SM;   // 16 (the 128px column takes 8px side pads)
static const int SHINE_BAR_W = TOK_LAYOUT_COL_LEFT_W - 2 * TOK_SPACE_SM;  // 112
static const int SHINE_BAR_H = TOK_METER_SM;
static int shineBarX[2] = {SHINE_BAR_X, SHINE_BAR_X};  // set by drawLimitCard (pace meter lefts)
static int shineBarY[2] = {-1, -1};           // set by drawLimitCard (pace meter tops)
static int shineBarW = SHINE_BAR_W;           // set by drawLimitCard (the meters' width)
static volatile int shineFillPx[2] = {-1, -1};  // fill widths cached under stateMutex
static uint32_t sweepStartMs = 0;
static bool sweepActive = false;
static int shinePrevCenter[2] = {INT_MIN, INT_MIN};

static inline uint16_t shineColor(int i, int center) {
  int d = abs(i - center);
  return d <= 2 ? TOK_COLOR_DATA_PACE_SHINE_HI : d <= 6 ? TOK_COLOR_DATA_PACE_SHINE_MID
       : d <= SHINE_BAND_R ? TOK_COLOR_DATA_PACE_SHINE_LO : TOK_COLOR_DATA_PACE;
}

// Paint the band over one pace meter's filled interior (columns 2..fillW-3,
// so the round ends stay untouched). Returns true if any column changed.
bool drawShineStrip(int barIdx, int center, bool force) {
  int fillW = shineFillPx[barIdx], y = shineBarY[barIdx];
  if (fillW < 14 || y < 0) return false;
  int prev = force ? INT_MIN : shinePrevCenter[barIdx];
  if (prev == center) return false;
  shinePrevCenter[barIdx] = center;
  bool changed = false;
  for (int i = 2; i <= fillW - 3; i++) {
    uint16_t c = shineColor(i, center);
    if (prev != INT_MIN && c == shineColor(i, prev)) continue;
    g->fillRect(shineBarX[barIdx] + i, y, 1, SHINE_BAR_H, c);
    changed = true;
  }
  return changed;
}

bool limitCardsPage() {
  return currentPage == 0 || currentPage == MIXED_PAGE || currentPage == NOTE_PAGE || currentPage == OVERVIEW_PAGE;
}

int sweepCenter(uint32_t nowMs) {
  float t = (float)(nowMs - sweepStartMs) / (TOK_MOTION_SWEEP_MS * motionTimeScale);
  if (t > 1.0f) t = 1.0f;
  return -SHINE_BAND_R + (int)(t * (shineBarW + 2 * SHINE_BAND_R));
}

bool shineTick(uint32_t nowMs) {
  // One sweep every TOK_MOTION_SWEEP_PERIOD_MS, measured start to start.
  if (!sweepActive && !cfgReduceMotion && cfgShowCountdown && limitCardsPage() &&
      (sweepStartMs == 0 || nowMs - sweepStartMs >= TOK_MOTION_SWEEP_PERIOD_MS * motionTimeScale)) {
    sweepActive = true;
    sweepStartMs = nowMs ? nowMs : 1;
    shinePrevCenter[0] = shinePrevCenter[1] = INT_MIN;
  }
  if (!sweepActive) return false;
  if (!limitCardsPage() || !cfgShowCountdown) { sweepActive = false; return false; }
  int center = sweepCenter(nowMs);
  bool done = center >= shineBarW + SHINE_BAND_R;
  bool changed = false;
  for (int i = 0; i < 2; i++)
    if (drawShineStrip(i, center, false)) changed = true;
  if (done) sweepActive = false;
  return changed;
}

// ── SHARED LEFT COLUMN (status / mixed / note pages) ───────
// SESSION_WINDOW_SEC / WEEK_WINDOW_SEC live in state.h (the sound alerts use them).

// Pace maths (elapsedPercentOfWindow, paceSecondsToLimit) lives in pace.cpp.

// The pace flag's projection text: "45m" / "3h", or "" (see pace.h).
String formatPaceDur(int currentPct, long elapsedSec, long remainingSec) {
  char b[16];
  formatPaceDurBuf(b, sizeof(b), currentPct, elapsedSec, remainingSec);
  return String(b);
}

// Metric flag (design.md 11.3): "!" (headline, status.error) + the projection
// (body, secondary) on the value's baseline -- earned only by pace, never by
// level (callers gate `ahead` on actual > pace + deadband).
// The projection is dropped when it would cross xMax (the narrow column keeps the "!").
void drawPaceFlag(int x, int baseline, bool ahead, int currentPct, long elapsedSec, long remainingSec, int xMax) {
  if (!ahead) return;
  int x2 = drawText(TOK_TYPE_HEADLINE, x, baseline - fontAscent(TOK_TYPE_HEADLINE), "!", TOK_COLOR_STATUS_ERROR);
  String dur = formatPaceDur(currentPct, elapsedSec, remainingSec);
  if (dur.length() > 0 && x2 + TOK_SPACE_HAIR + textW(TOK_TYPE_BODY, dur) <= xMax)
    drawText(TOK_TYPE_BODY, x2 + TOK_SPACE_HAIR, baseline - fontAscent(TOK_TYPE_BODY), dur, TOK_COLOR_TEXT_SECONDARY);
}

// One limit card (design.md 7.4): numeral.lg % + label (+ flag), the paired
// meter (usage over pace), then two caption lines -- when it resets, and in
// how long. Compact (the Overview's 89px cells, design.md 7.5) puts both on
// one caption row. countdown is "4h 03m" (no "in"), "" when unknown.
static void drawLimitCard(int cardX, int cardY, int cardW, int cardH, bool compact, const char* label,
                          int percent, bool ahead, long elapsed, long rem, int pace, int barIdx,
                          const char* resets, const String& countdown) {
  drawCard(cardX, cardY, cardW, cardH);
  const int x = cardX + TOK_SPACE_SM, w = cardW - 2 * TOK_SPACE_SM;  // 8px side pads
  // numeral.lg ink starts 6px below its line top and the last caption's ends at
  // +94: 10px of air above and below the ink.
  const int top = cardY + TOK_SPACE_XS;
  const int baseline = top + fontAscent(TOK_TYPE_NUMERAL_LG);
  int xe;
  // Stale (design.md 13.4): the numeral steps down to text.secondary, so the
  // glance doesn't read an old % as live; the strip says how old.
  if (percent >= 0) xe = drawText(TOK_TYPE_NUMERAL_LG, x, top, String(percent) + "%",
                                  dataStale() ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_DATA_USAGE);
  else xe = drawText(TOK_TYPE_NUMERAL_LG, x, top, "--", TOK_COLOR_TEXT_TERTIARY);
  int lx = xe + TOK_SPACE_SM;
  drawSectionLabel(lx, baseline - fontAscent(TOK_TYPE_LABEL), label);
  drawPaceFlag(lx + textW(TOK_TYPE_LABEL, label) + TOK_SPACE_XS, baseline, ahead, percent, elapsed, rem, x + w);

  int y = top + fontLineH(TOK_TYPE_NUMERAL_LG) + TOK_SPACE_STACK_TIGHT;
  drawMeter(x, y, w, TOK_METER_MD, percent, TOK_COLOR_DATA_USAGE);
  y += TOK_METER_MD + TOK_SPACE_STACK_TIGHT;
  // The pace meter is optional (Settings > Pace bars); off, the space closes up.
  if (cfgShowCountdown) {
    shineBarX[barIdx] = x;
    shineBarY[barIdx] = y;
    shineBarW = w;
    shineFillPx[barIdx] = drawMeter(x, y, w, SHINE_BAR_H, pace, TOK_COLOR_DATA_PACE);
    if (sweepActive) drawShineStrip(barIdx, sweepCenter(millis()), true);
    y += SHINE_BAR_H + TOK_SPACE_STACK_TIGHT;
  } else {
    shineBarY[barIdx] = -1;
    shineFillPx[barIdx] = -1;
  }

  // Compact: the countdown right-aligned on the reset time's row. Measured
  // fit, shortest last: "Resets 16:30 .. in 4h 03m" (157px) -> "16:30 .. in
  // 4h 03m" (108) -> "Thu 04:59 .. 6d 16h" (121, the week's).
  String inText = countdown.length() ? "in " + countdown : String("");
  int rightW = 0;
  if (compact && inText.length()) {
    rightW = TOK_SPACE_SM + textW(TOK_TYPE_CAPTION, inText);
    if (textW(TOK_TYPE_CAPTION, resets) + rightW > w) {
      inText = countdown;
      rightW = TOK_SPACE_SM + textW(TOK_TYPE_CAPTION, inText);
    }
  }
  // "Resets Thu 04:59" is 113px, 1 over the 112 inner: it drops the word.
  const bool fitsWord = textW(TOK_TYPE_CAPTION, "Resets ") + textW(TOK_TYPE_CAPTION, resets) + rightW <= w;
  int rx = fitsWord ? drawText(TOK_TYPE_CAPTION, x, y, "Resets ", TOK_COLOR_TEXT_SECONDARY) : x;
  if (resets[0] != '\0') drawText(TOK_TYPE_CAPTION, rx, y, resets, TOK_COLOR_TEXT_SECONDARY);
  else drawText(TOK_TYPE_CAPTION, rx, y, "--", TOK_COLOR_TEXT_TERTIARY);
  if (!inText.length()) return;
  if (compact) drawTextR(TOK_TYPE_CAPTION, x + w, y, inText, TOK_COLOR_TEXT_SECONDARY);
  else drawText(TOK_TYPE_CAPTION, x, y + 17, inText, TOK_COLOR_TEXT_SECONDARY);
}

// Week reset label: the server's "Oct 1, 04:59" with the date swapped for the
// weekday name ("Thu 04:59"), computed locally from the live countdown so it
// never depends on the server's clock. Falls back to the server string
// verbatim when the countdown or the comma (time-of-day suffix) is missing.
String weekResetWeekdayLabel(long weekRem, const char* resets) {
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

// The 5H and WK cards: the left column, or the Overview's two top-left cells.
void drawLimitsColumn(bool overview) {
  long sessionRem = liveResetsInSec(STATE.sessionResetsInSec);
  long weekRem = liveResetsInSec(STATE.weekResetsInSec);
  String weekResetLabel = weekResetWeekdayLabel(weekRem, STATE.weekResets);

  // QUOTA PACING: "% of the window elapsed" (the pace meter) vs actual
  // usage -- a flag is earned only by running ahead of pace.
  int sessionPace = elapsedPercentOfWindow(sessionRem, SESSION_WINDOW_SEC);
  int weekPace = elapsedPercentOfWindow(weekRem, WEEK_WINDOW_SEC);
  bool sessionAhead = paceAhead(STATE.sessionPercent, sessionPace);  // deadband stops boundary flicker
  bool weekAhead = paceAhead(STATE.weekPercent, weekPace);
  long sessionElapsed = sessionRem >= 0 ? SESSION_WINDOW_SEC - sessionRem : -1;
  long weekElapsed = weekRem >= 0 ? WEEK_WINDOW_SEC - weekRem : -1;

  const int x0 = overview ? OV_COL_X[0] : TOK_LAYOUT_COL_LEFT_X, w0 = overview ? OV_COL_W[0] : TOK_LAYOUT_COL_LEFT_W;
  const int x1 = overview ? OV_COL_X[1] : TOK_LAYOUT_COL_LEFT_X, w1 = overview ? OV_COL_W[1] : TOK_LAYOUT_COL_LEFT_W;
  const int y0 = overview ? OV_ROW_Y[0] : LIMIT5H_Y, y1 = overview ? OV_ROW_Y[0] : LIMITWK_Y;
  const int h = overview ? OV_ROW_H[0] : LIMIT_CARD_H;
  drawLimitCard(x0, y0, w0, h, overview, "5H", STATE.sessionPercent, sessionAhead, sessionElapsed, sessionRem,
                sessionPace, 0, STATE.sessionResets, sessionRem >= 0 ? fmtCountdown(sessionRem) : String(""));
  drawLimitCard(x1, y1, w1, h, overview, "WK", STATE.weekPercent, weekAhead, weekElapsed, weekRem,
                weekPace, 1, weekResetLabel.c_str(), weekRem >= 0 ? fmtCountdownDHM(weekRem) : String(""));
}

// Bitcoin logo: 15px orange disc with a black (bg.canvas) pixel-drawn B and two ticks
// (top and bottom), centred on (cx, cy). Twin of simulator-s3.html's.
static const int BTC_LOGO_R = 7;
void drawBtcLogo(int cx, int cy) {
  static const char* const B[9] = {
    ".X.X.",
    "XXXX.",
    "X...X",
    "X...X",
    "XXXX.",
    "X...X",
    "X...X",
    "XXXX.",
    ".X.X."};
  aaFillCircle(cx, cy, BTC_LOGO_R, TOK_BTC_ORANGE);
  for (int r = 0; r < 9; r++)
    for (int c = 0; c < 5; c++)
      if (B[r][c] == 'X') g->drawPixel(cx - 2 + c, cy - 4 + r, TOK_COLOR_BG_CANVAS);
}

// BTC (compact card), two rows with 8px of air between and around the ink: the
// logo and the 24h change (right-aligned to the content edge; green up, red
// down, the sign carries it without colour), then the price in numeral.lg on
// the full inner width (112px in the left column). A price too wide for it
// steps down to md, then sm. The rows keep the 60px card's places in a taller
// box (the Overview cell): the extra height goes to the bottom (design.md 7.2).
void drawBtcCard(int cardX, int cardY, int cardW, int cardH) {
  drawCard(cardX, cardY, cardW, cardH);
  const int x = cardX + TOK_SPACE_SM, w = cardW - 2 * TOK_SPACE_SM;
  const int cy = cardY + TOK_SPACE_SM + BTC_LOGO_R;
  drawBtcLogo(x + BTC_LOGO_R, cy);
  if (STATE.btcPrice >= 0 && !isnan(STATE.btcChangePct))
    drawTextR(TOK_TYPE_NUMERAL_SM, x + w, cy - fontLineH(TOK_TYPE_NUMERAL_SM) / 2,
              fmtChangePct(STATE.btcChangePct),
              STATE.btcChangePct < 0 ? TOK_COLOR_STATUS_ERROR : TOK_COLOR_STATUS_SUCCESS);
  // numeral.lg ink ends 27px below its line top.
  const int baseline = cardY + BTC_CARD_H - TOK_SPACE_SM - 27 + fontAscent(TOK_TYPE_NUMERAL_LG);
  if (STATE.btcPrice < 0) {
    drawText(TOK_TYPE_NUMERAL_LG, x, baseline - fontAscent(TOK_TYPE_NUMERAL_LG), "--", TOK_COLOR_TEXT_TERTIARY);
    return;
  }
  String price = fmtBtc(STATE.btcPrice);
  FontId pf = TOK_TYPE_NUMERAL_LG;
  if (textW(pf, price) > w) pf = TOK_TYPE_NUMERAL_MD;
  if (textW(pf, price) > w) pf = TOK_TYPE_NUMERAL_SM;
  drawText(pf, x, baseline - fontAscent(pf), price, TOK_COLOR_TEXT_PRIMARY);
}


// ── ANALOG CLOCK (design.md 11.18) ─────────────────────────
// Filled pace wedge from the hour hand clockwise to the reset angle.
void drawTimerWedge(int cx, int cy, int r, float startAngle, float endAngle) {
  float delta = fmodf(endAngle - startAngle, 360.0f);
  if (delta < 0) delta += 360.0f;
  g->fillArc(cx, cy, r, 0, startAngle, startAngle + delta, TOK_COLOR_DATA_PACE_WEDGE);
}

// Face (2px AA ring), 12 ticks, hour/minute hands, the pace wedge (Pace bars
// only), the green 5h-reset radius, the accent second hand, the hub. Angles
// are screen-space with -90deg so 0 points up.
void drawAnalogClock(int cx, int cy, int r, int hour24, int minute, int second,
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
void aqiColors(int aqi, uint16_t& bg, uint16_t& fg) {
  fg = TOK_GRAY_0;
  if (aqi <= 50)       bg = TOK_AQI_GOOD;
  else if (aqi <= 100) bg = TOK_AQI_MODERATE;
  else if (aqi <= 150) bg = TOK_AQI_USG;
  else if (aqi <= 200) bg = TOK_AQI_UNHEALTHY;
  else if (aqi <= 300) bg = TOK_AQI_VERY_UNHEALTHY;
  else               { bg = TOK_AQI_HAZARDOUS; fg = TOK_GRAY_6; }
}

int aqiBadgeW(int aqi) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", aqi);
  return textW(TOK_TYPE_HEADLINE, buf) + 2 * TOK_BADGE_PAD_X;
}
void drawAqiBadge(int x, int y, int aqi) {
  char buf[8];
  snprintf(buf, sizeof(buf), "%d", aqi);
  uint16_t bg, fg;
  aqiColors(aqi, bg, fg);
  aaFillRoundRect(x, y, aqiBadgeW(aqi), TOK_BADGE_H, TOK_RADIUS_SM, bg);
  drawText(TOK_TYPE_HEADLINE, x + TOK_BADGE_PAD_X, y + (TOK_BADGE_H - fontLineH(TOK_TYPE_HEADLINE)) / 2, buf, fg);
}


// Minimal month grid (Sunday first, no header): 7 columns x 20px, rows 20px.
// This month's weekdays primary, weekends secondary, the neighbouring months'
// days tertiary; today is white on an accent disc. 5 or 6 rows as needed.
// Thai public holidays, (month << 8) | day per year (twin of the simulator's
// THAI_HOLIDAYS). Lunar dates move yearly; add new years by hand.
struct HolidayYear { int year; uint8_t n; const uint16_t *days; };
static const uint16_t TH_2026[] = {0x0101,0x0303,0x0406,0x040D,0x040E,0x040F,0x0501,0x0504,0x051F,0x0603,0x071C,0x071D,0x071E,0x080C,0x0A0D,0x0A17,0x0C05,0x0C0A,0x0C1F};
static const uint16_t TH_2027[] = {0x0101,0x0214,0x0406,0x040D,0x040E,0x040F,0x0501,0x0504,0x0514,0x0603,0x0712,0x0713,0x071C,0x080C,0x0A0D,0x0A17,0x0C05,0x0C0A,0x0C1F};
static const HolidayYear THAI_HOLIDAYS[] = {
  {2026, sizeof(TH_2026) / 2, TH_2026},
  {2027, sizeof(TH_2027) / 2, TH_2027},
};
bool isThaiHoliday(int year, int mon, int d) {
  const uint16_t key = ((mon + 1) << 8) | d;
  for (const HolidayYear &h : THAI_HOLIDAYS)
    if (h.year == year)
      for (uint8_t i = 0; i < h.n; i++) if (h.days[i] == key) return true;
  return false;
}

int daysInMonth(int m, int yr) {
  static const uint8_t DIM[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  bool leap = (yr % 4 == 0 && yr % 100 != 0) || yr % 400 == 0;
  return (int)DIM[m] + (m == 1 && leap ? 1 : 0);
}

// One calendar day centred on (cx, cy): day d of month `mon` (d < 1 or past
// the month's end = a neighbouring month's day), in weekday column c.
void drawCalendarDay(int cx, int cy, int year, int mon, int d, int c, int mday) {
  const int dim = daysInMonth(mon, year);
  const int prevDim = mon == 0 ? daysInMonth(11, year - 1) : daysInMonth(mon - 1, year);
  bool other = d < 1 || d > dim;
  int shown = d < 1 ? prevDim + d : d > dim ? d - dim : d;
  uint16_t col = other ? TOK_COLOR_TEXT_TERTIARY
               : isThaiHoliday(year, mon, d) ? TOK_COLOR_HOLIDAY
               : (c == 0 || c == 6) ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_PRIMARY;
  if (!other && d == mday) {
    aaFillCircle(cx, cy, CAL_DISC_R, TOK_COLOR_ACCENT);
    col = TOK_COLOR_TEXT_PRIMARY;
  }
  char nb[4];
  snprintf(nb, sizeof(nb), "%d", shown);
  drawTextC(TOK_TYPE_CALENDAR, cx, cy - fontLineH(TOK_TYPE_CALENDAR) / 2, nb, col);
}

static int firstWeekdayOfMonth(int mday, int wday) { return (((wday - (mday - 1)) % 7) + 7) % 7; }
int monthGridRows(int year, int mon, int mday, int wday) {
  return (firstWeekdayOfMonth(mday, wday) + daysInMonth(mon, year) + 6) / 7;
}

// colW-wide columns; rows rowH tall, or rowH6 in a 6-row month.
void drawMonthGrid(int x, int y, int colW, int rowH, int rowH6, int year, int mon, int mday, int wday) {
  const int first = firstWeekdayOfMonth(mday, wday);
  const int rows = monthGridRows(year, mon, mday, wday);
  if (rows > 5) rowH = rowH6;
  for (int r = 0; r < rows; r++)
    for (int c = 0; c < 7; c++)
      drawCalendarDay(x + c * colW + colW / 2, y + r * rowH + rowH / 2, year, mon, r * 7 + c - first + 1, c, mday);
}

// Forecast columns (the status strip, the Overview's weather cell) from the
// label row's top iy: label caption 17, the glyph centred 34 down, the
// temperature 51 down, 68 in all.

// "Now" (a 44px column from x) and, past a 1px divider, the next `hours` hours
// in 24px columns.
void drawWeatherNowAndHours(int x, int iy, int hours) {
  // "Now": the current-item marker is accent.
  const int nowCx = x + 22;
  drawTextC(TOK_TYPE_CAPTION, nowCx, iy, "Now", TOK_COLOR_ACCENT);
  drawWeatherIcon(nowCx, iy + WX_GLYPH_DY, STATE.weatherCode, 1.2f);
  drawTemp(TOK_TYPE_NUMERAL_SM, nowCx, iy + WX_TEMP_DY, (int)round(STATE.weatherTempC), STATE.weatherTempC > -900);
  // 1px divider on the now | next-hour column boundary, the content box's height.
  g->fillRect(x + 44, iy, 1, WX_ROWS_H, TOK_COLOR_TEXT_TERTIARY);

  // weatherHourly[] starts at the current hour (the "now" column), so the
  // next hours are indices 1..hours. 24px columns take a 0.9 glyph (~18px).
  for (int i = 0; i < hours; i++) {
    int idx = i + 1;
    int cx = x + 44 + i * 24 + 12;
    bool have = STATE.weatherHourlyCount > idx;
    char hbuf[4];
    if (have) snprintf(hbuf, sizeof(hbuf), "%02d", STATE.weatherHourly[idx].hour);
    drawTextC(TOK_TYPE_NUMERAL_SM, cx, iy, have ? hbuf : "--",
              have ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_TERTIARY);
    drawWeatherIcon(cx, iy + WX_GLYPH_DY, have ? STATE.weatherHourly[idx].code : -1, 0.9f);
    drawTemp(TOK_TYPE_NUMERAL_SM, cx, iy + WX_TEMP_DY, have ? STATE.weatherHourly[idx].tempC : 0, have);
  }
}

// The 5H window's reset as a wall-clock time, for the analog clocks' wedge.
bool sessionResetTime(const struct tm& t, int& hour, int& minute) {
  long rem = liveResetsInSec(STATE.sessionResetsInSec);
  if (rem < 0) return false;
  long totalSec = ((long)t.tm_hour * 3600 + t.tm_min * 60 + t.tm_sec + rem) % 86400;
  hour = totalSec / 3600;
  minute = (totalSec % 3600) / 60;
  return true;
}
