// Usage page (page 1).
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"

// ── DATA ROWS (Usage page limits column, Device page) ─────

// A limits-column row in 112 px: label (body) left, percent (numeral.sm, on the
// label's baseline) right, meter.md under it. No detail text -- the Status
// page's 5H / Week cards carry the "Resets ..." lines. numeral.sm, not .md,
// because "Context" + "100%" at .md is wider than the column.
static void drawLimitRow(int x, int r, int y, const char* label, int percent) {
  String pct = percent >= 0 ? String(percent) + "%" : String("--");
  int pw = textW(TOK_TYPE_NUMERAL_SM, pct);
  drawText(TOK_TYPE_BODY, x, y, fitText(TOK_TYPE_BODY, label, r - x - pw - TOK_SPACE_XS), TOK_COLOR_TEXT_PRIMARY);
  // Stale: secondary, as the limit cards' numerals (drawLimitCard).
  drawTextR(TOK_TYPE_NUMERAL_SM, r, y + fontAscent(TOK_TYPE_BODY) - fontAscent(TOK_TYPE_CAPTION), pct,
            percent < 0 ? TOK_COLOR_TEXT_TERTIARY : dataStale() ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_PRIMARY);
  drawMeter(x, y + 23 + TOK_SPACE_STACK_TIGHT, r - x, TOK_METER_MD, percent, TOK_COLOR_DATA_USAGE);
}

// ── USAGE PAGE (page 1) ────────────────────────────────────
// The old Projects and Limits pages on one page, in the Status page's grid:
//  - left column (x 8..135): one card, the /usage panel -- context window,
//    5-hour, weekly, weekly per-model (hidden when the server sends null), usage
//    credits. Rows close up when one is absent.
//  - right column (x 144..471): two cards (one idea each), top projects then the
//    7-day trend. Project rows are single-line (design.md 11.6's dense variant):
//    name (body), meter.md, value (caption) right.
static const int PROJ_CARD_H = 171, TREND_CARD_H = 105;
static const int TREND_CARD_Y = TOK_LAYOUT_CONTENT_Y0 + PROJ_CARD_H + TOK_SPACE_GUTTER;  // 187
static const int PROJ_ROW_STEP = 23 + TOK_SPACE_STACK;                                  // 31
static const int PROJ_VALUE_W = 56;
static const int PROJ_NAME_W = 116;                   // project name column (fitText)
static const int TREND_BAR_W = 20, TREND_BAR_GAP = 4; // 7 bars = 164 px from the align line

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

void drawUsagePage() {
  // ── limits column ──
  const int lx0 = TOK_LAYOUT_COL_LEFT_X, lw = TOK_LAYOUT_COL_LEFT_W;
  drawCard(lx0, TOK_LAYOUT_CONTENT_Y0, lw, TOK_LAYOUT_CONTENT_Y1 - TOK_LAYOUT_CONTENT_Y0);
  const int ix = lx0 + TOK_SPACE_SM, ir = lx0 + lw - TOK_SPACE_SM;  // 16..128: 112 inner
  const int ltop = TOK_LAYOUT_CONTENT_Y0 + TOK_SPACE_CARD_PAD;
  drawSectionLabel(ix, ltop, "LIMITS");
  int ly = ltop + 17 + TOK_SPACE_SM;
  drawLimitRow(ix, ir, ly, "Context", STATE.ctxTokens >= 0 ? STATE.ctxPercent : -1);
  ly += ROW_STEP;
  drawLimitRow(ix, ir, ly, "5-hour", STATE.sessionPercent);
  ly += ROW_STEP;
  drawLimitRow(ix, ir, ly, "Weekly", STATE.weekPercent);
  ly += ROW_STEP;
  if (STATE.weekModelPercent >= 0) {
    drawLimitRow(ix, ir, ly, STATE.weekModelName[0] != '\0' ? STATE.weekModelName : "Model", STATE.weekModelPercent);
    ly += ROW_STEP;
  }
  if (STATE.creditsUsed >= 0) drawLimitRow(ix, ir, ly, "Credits", STATE.creditsPercent);

  // ── right column: top projects ──
  const int x0 = TOK_LAYOUT_COL_RIGHT_X, w = TOK_LAYOUT_COL_RIGHT_W;
  drawCard(x0, TOK_LAYOUT_CONTENT_Y0, w, PROJ_CARD_H);
  const int px = x0 + TOK_SPACE_CARD_PAD, pr = x0 + w - TOK_SPACE_CARD_PAD;  // 156..460
  const int alignX = px + PROJ_NAME_W + TOK_SPACE_SM;  // meters and trend bars share this line
  const int top = TOK_LAYOUT_CONTENT_Y0 + TOK_SPACE_CARD_PAD;
  drawSectionLabel(px, top, "TOP PROJECTS 7D");

  if (STATE.projectCount == 0) {
    int ey = top + 17;
    drawEmptyState(x0 + w / 2, ey, TOK_LAYOUT_CONTENT_Y0 + PROJ_CARD_H - ey, "No project data yet",
                   "Projects appear after the first poll", false);
  } else {
    int shown = STATE.projectCount < 4 ? STATE.projectCount : 4;
    int64_t maxTokens = 1;
    for (int i = 0; i < shown; i++)
      if (STATE.projectTokens[i] > maxTokens) maxTokens = STATE.projectTokens[i];
    const int meterW = pr - PROJ_VALUE_W - TOK_SPACE_SM - alignX;
    int y = top + 17 + TOK_SPACE_SM;
    for (int i = 0; i < shown; i++, y += PROJ_ROW_STEP) {
      drawText(TOK_TYPE_BODY, px, y, fitText(TOK_TYPE_BODY, STATE.projectNames[i], PROJ_NAME_W), TOK_COLOR_TEXT_PRIMARY);
      int pct = (int)((float)STATE.projectTokens[i] / maxTokens * 100 + 0.5f);
      drawMeter(alignX, y + 12 - TOK_METER_MD / 2, meterW, TOK_METER_MD, pct, TOK_COLOR_DATA_USAGE);
      // Caption value on the name's baseline.
      drawTextR(TOK_TYPE_NUMERAL_SM, pr, y + fontAscent(TOK_TYPE_BODY) - fontAscent(TOK_TYPE_CAPTION),
                fmtTokens(STATE.projectTokens[i]), TOK_COLOR_TEXT_SECONDARY);
    }
  }

  // ── right column: 7-day trend (compact card): label column left, bars from alignX ──
  drawCard(x0, TREND_CARD_Y, w, TREND_CARD_H);
  const int ty = TREND_CARD_Y + TOK_SPACE_CARD_PAD_COMPACT_V;
  drawSectionLabel(px, ty, "7-DAY TREND");
  int64_t total = 0, maxTrend = 1;
  for (int i = 0; i < 7; i++) {
    total += STATE.trend[i];
    if (STATE.trend[i] > maxTrend) maxTrend = STATE.trend[i];
  }
  drawText(TOK_TYPE_BODY, px, ty + 17 + TOK_SPACE_SM, fmtTokens(total), TOK_COLOR_TEXT_PRIMARY);
  drawText(TOK_TYPE_CAPTION, px, ty + 17 + TOK_SPACE_SM + 23, "this week", TOK_COLOR_TEXT_SECONDARY);

  static const char* const DAY_LABELS[7] = {"-6", "-5", "-4", "-3", "-2", "-1", "Now"};
  const int barW = TREND_BAR_W, gap = TREND_BAR_GAP;
  const int axisY = TREND_CARD_Y + TREND_CARD_H - TOK_SPACE_CARD_PAD_COMPACT_V - 17;
  const int feet = axisY - TOK_SPACE_XS;  // bars end above the axis labels
  const int chartH = feet - ty;
  for (int i = 0; i < 7; i++) {
    int bx = alignX + i * (barW + gap);
    int bh = (int)((float)STATE.trend[i] / maxTrend * chartH);
    if (bh < 4) bh = 4;
    aaFillRoundRect(bx, feet - bh, barW, bh, TOK_RADIUS_SM, TOK_COLOR_DATA_USAGE);
    drawTextC(TOK_TYPE_CAPTION, bx + barW / 2, axisY, DAY_LABELS[i],
              i == 6 ? TOK_COLOR_ACCENT : TOK_COLOR_TEXT_TERTIARY);
  }
}
