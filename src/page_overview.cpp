// Overview page (page 6, design.md 7.5).
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"

// ── OVERVIEW PAGE (OVERVIEW_PAGE, design.md 7.5) ───────────
// The cards in miniature on a 3x3 grid (state.h's OV_*), row by row: 5H, WK,
// clock / date, month calendar, BTC / weather, cat, note. The cat cell is the GIF
// player's pane, so the page draws as drawMediaPageStatic(), like Mixed.
// Every cell takes 8px pads (the weather cell its compact 8 / 12).
static const int OV_CLOCK_R = 30;
static const int OV_CLOCK_CX = OV_COL_X[2] + TOK_SPACE_SM + OV_CLOCK_R;   // 361
static const int OV_CLOCK_CY = OV_ROW_Y[0] + OV_ROW_H[0] / 2;              // 52
// Month grid: 7 x 19 = 133 of the 134 inner; 14px rows (5 rows = 70 of the
// 74 inner), 13 in a 6-row month (78: into the pads, 6px each side).
static const int OV_CAL_COL = 19, OV_CAL_ROW = 14, OV_CAL_ROW6 = 13;

// Clock: the dial (r 30, with the reset wedge and radius) and, 8px right of
// it, the digital time centred on the dial. The time's ink starts below y 40
// and the dial's above it stays left of x 396: both clear of the corner slots.
static void drawOverviewClock(bool haveTime, const struct tm& t) {
  drawCard(OV_COL_X[2], OV_ROW_Y[0], OV_COL_W[2], OV_ROW_H[0]);
  const int tx = OV_CLOCK_CX + OV_CLOCK_R + 1 + TOK_SPACE_SM;                // 400
  const int ty = OV_CLOCK_CY - fontLineH(TOK_TYPE_CLOCK) / 2;
  if (!haveTime) {
    aaRing(OV_CLOCK_CX, OV_CLOCK_CY, OV_CLOCK_R, 2, TOK_COLOR_TEXT_TERTIARY);
    drawText(TOK_TYPE_CLOCK, tx, ty, "--:--", TOK_COLOR_TEXT_TERTIARY);
    return;
  }
  int resetHour = 0, resetMinute = 0;
  bool haveReset = sessionResetTime(t, resetHour, resetMinute);
  drawAnalogClock(OV_CLOCK_CX, OV_CLOCK_CY, OV_CLOCK_R, t.tm_hour, t.tm_min, t.tm_sec,
                  haveReset, resetHour, resetMinute);
  char hm[8];
  snprintf(hm, sizeof(hm), "%02d:%02d", t.tm_hour, t.tm_min);
  drawText(TOK_TYPE_CLOCK, tx, ty, hm, TOK_COLOR_TEXT_PRIMARY);
}

// Date (hero readout, so centred in the card): the weekday in full (headline,
// accent -- the current-item marker), then the day of the month in type.display
// with "Oct 2026" (caption) on its baseline. 17px of air above and below the ink.
static void drawOverviewDate(bool haveTime, const struct tm& t) {
  const int x = OV_COL_X[0], y = OV_ROW_Y[1];
  drawCard(x, y, OV_COL_W[0], OV_ROW_H[1]);
  const int tx = x + TOK_SPACE_SM;
  const int numY = y + 33;                                   // display ink y+42..72
  const int baseline = numY + fontAscent(TOK_TYPE_DISPLAY);
  if (!haveTime) {
    drawText(TOK_TYPE_DISPLAY, tx, numY, "--", TOK_COLOR_TEXT_TERTIARY);
    return;
  }
  drawText(TOK_TYPE_HEADLINE, tx, y + 12, WDAY_FULL[t.tm_wday], TOK_COLOR_ACCENT);
  char nb[4];
  snprintf(nb, sizeof(nb), "%d", t.tm_mday);
  const int numEnd = drawText(TOK_TYPE_DISPLAY, tx, numY, nb, TOK_COLOR_TEXT_PRIMARY);
  char my[12];
  snprintf(my, sizeof(my), "%s %d", MON_ABBR[t.tm_mon], t.tm_year + 1900);
  drawText(TOK_TYPE_CAPTION, numEnd + TOK_SPACE_SM, baseline - fontAscent(TOK_TYPE_CAPTION), my,
           TOK_COLOR_TEXT_SECONDARY);
}

// Month calendar: the status page's month grid at 19px columns, centred in
// the card (a figure, not text, so it is the exception to top-anchoring).
static void drawOverviewCalendar(bool haveTime, const struct tm& t) {
  const int x = OV_COL_X[1], y = OV_ROW_Y[1], w = OV_COL_W[1], h = OV_ROW_H[1];
  drawCard(x, y, w, h);
  if (!haveTime) return;
  const int year = t.tm_year + 1900;
  const int rows = monthGridRows(year, t.tm_mon, t.tm_mday, t.tm_wday);
  const int gridH = rows * (rows > 5 ? OV_CAL_ROW6 : OV_CAL_ROW);
  drawMonthGrid(x + (w - 7 * OV_CAL_COL) / 2, y + (h - gridH) / 2, OV_CAL_COL, OV_CAL_ROW, OV_CAL_ROW6,
                year, t.tm_mon, t.tm_mday, t.tm_wday);
}

// Weather (compact card, tappable -> Weather sheet): Now and the next 3 hours.
static void drawOverviewWeather() {
  const uint16_t bg = (pressedId == PRESS_WEATHER) ? TOK_COLOR_SURFACE_RAISED : TOK_COLOR_SURFACE_CARD;
  drawCardSurface(OV_COL_X[0], OV_ROW_Y[2], OV_COL_W[0], OV_ROW_H[2], bg);
  drawWeatherNowAndHours(OV_COL_X[0] + TOK_SPACE_CARD_PAD_COMPACT_H, OV_ROW_Y[2] + TOK_SPACE_CARD_PAD_COMPACT_V, 3);
}

// Note: the text in mono.s whatever the editor's size (19 x 4), no label.
static void drawOverviewNote() {
  const int x = OV_COL_X[2], y = OV_ROW_Y[2], w = OV_COL_W[2], h = OV_ROW_H[2];
  drawCard(x, y, w, h);
  if (STATE.note[0] == '\0') {
    drawEmptyState(x + w / 2, y, h, "No note yet", "Edit at :8787/note", false);
    return;
  }
  drawNoteText(x + TOK_SPACE_SM, y + TOK_SPACE_SM, w - 2 * TOK_SPACE_SM, y + h - TOK_SPACE_SM, TOK_TYPE_MONO_S);
}

void drawOverviewCells() {
  struct tm t;
  bool haveTime = haveLocalTime(&t);
  drawLimitsColumn(true);
  drawOverviewClock(haveTime, t);
  drawOverviewDate(haveTime, t);
  drawOverviewCalendar(haveTime, t);
  drawBtcCard(OV_COL_X[2], OV_ROW_Y[1], OV_COL_W[2], OV_ROW_H[1]);
  drawOverviewWeather();
  drawOverviewNote();
}
