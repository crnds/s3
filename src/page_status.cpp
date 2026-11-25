// Status page (page 0, design.md 7.4).
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"

// Calendar-card footer: full-width bar in the AQI colour, "AQI" left and the
// index right-aligned (a category word does not fit 135px). aqi < 0 draws a
// neutral placeholder.
static void drawAqiBar(int x, int y, int w, int aqi) {
  uint16_t bg = TOK_COLOR_FILL_TRACK, fg = TOK_COLOR_TEXT_TERTIARY;
  if (aqi >= 0) aqiColors(aqi, bg, fg);
  aaFillRoundRect(x, y, w, CAL_BAR_H, TOK_RADIUS_SM, bg);
  const int ty = y + (CAL_BAR_H - fontLineH(TOK_TYPE_CAPTION)) / 2;
  drawText(TOK_TYPE_LABEL, x + TOK_SPACE_SM, ty, "AQI", fg);
  if (aqi < 0) { drawTextR(TOK_TYPE_LABEL, x + w - TOK_SPACE_SM, ty, "--", fg); return; }
  char nb[8];
  snprintf(nb, sizeof(nb), "%d", aqi);
  drawTextR(TOK_TYPE_LABEL, x + w - TOK_SPACE_SM, ty, nb, fg);
}


void drawStatusPage() {
  drawLimitsColumn();
  drawBtcCard();
  drawCard(TOK_LAYOUT_COL_RIGHT_X, CLOCK_CARD_Y, CLOCK_CARD_W, CLOCK_CARD_H);
  drawCard(CAL_CARD_X, CLOCK_CARD_Y, CAL_CARD_W, CLOCK_CARD_H);
  const bool wxPressed = (pressedId == PRESS_WEATHER);
  const uint16_t wxBg = wxPressed ? TOK_COLOR_SURFACE_RAISED : TOK_COLOR_SURFACE_CARD;
  drawCardSurface(TOK_LAYOUT_COL_RIGHT_X, WEATHER_CARD_Y, TOK_LAYOUT_COL_RIGHT_W, WEATHER_CARD_H, wxBg);

  struct tm timeinfo;
  bool haveTime = haveLocalTime(&timeinfo);

  int resetHour = 0, resetMinute = 0;
  bool haveReset = haveTime && sessionResetTime(timeinfo, resetHour, resetMinute);

  if (haveTime) {
    drawAnalogClock(CLOCK_CX, CLOCK_CY, CLOCK_R, timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec,
                    haveReset, resetHour, resetMinute);
  } else {
    aaRing(CLOCK_CX, CLOCK_CY, CLOCK_R, 2, TOK_COLOR_TEXT_TERTIARY);
  }

  // Digital time (type.clock) centred under the dial.
  if (haveTime) {
    char hm[8];
    snprintf(hm, sizeof(hm), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    drawTextC(TOK_TYPE_CLOCK, CLOCK_CX, DIGITAL_Y, hm, TOK_COLOR_TEXT_PRIMARY);
  } else {
    drawTextC(TOK_TYPE_CLOCK, CLOCK_CX, DIGITAL_Y, "--:--", TOK_COLOR_TEXT_TERTIARY);
  }

  // Calendar card: "Tue 6" (type.title, weekday accent) on one baseline, "Oct 2026"
  // under it, the month grid, and the AQI bar in the bottom pad.
  if (haveTime) {
    const int numY = CAL_BASE_Y - fontAscent(TOK_TYPE_TITLE);
    const int wdEnd = drawText(TOK_TYPE_TITLE, CAL_X, numY, WDAY_ABBR[timeinfo.tm_wday], TOK_COLOR_ACCENT);
    char nb[4];
    snprintf(nb, sizeof(nb), "%d", timeinfo.tm_mday);
    drawText(TOK_TYPE_TITLE, wdEnd + TOK_SPACE_SM, numY, nb, TOK_COLOR_TEXT_PRIMARY);
    char my[12];
    snprintf(my, sizeof(my), "%s %d", MON_ABBR[timeinfo.tm_mon], timeinfo.tm_year + 1900);
    drawText(TOK_TYPE_CAPTION, CAL_X, CAL_BASE_Y + 1, my, TOK_COLOR_TEXT_SECONDARY);
    // 6-row months squeeze to 17px rows so the grid stays ~100px tall.
    drawMonthGrid(CAL_GRID_X, CAL_GRID_Y, CAL_COL, CAL_ROW, CAL_ROW_TALL, timeinfo.tm_year + 1900, timeinfo.tm_mon,
                  timeinfo.tm_mday, timeinfo.tm_wday);
  } else {
    drawText(TOK_TYPE_TITLE, CAL_X, CAL_BASE_Y - fontAscent(TOK_TYPE_TITLE), "--", TOK_COLOR_TEXT_TERTIARY);
  }
  if (cfgShowAqi) drawAqiBar(CAL_X, CAL_BAR_Y, CAL_BAR_W, STATE.aqi);

  // ── weather strip (compact card, tappable -> Weather sheet) ──
  // H/L 20 | now 44 | 10 x 24 hourly = 304 inner. No disclosure chevron
  // (design.md 11.1's exception); the whole card is still the target.
  const int ix = TOK_LAYOUT_COL_RIGHT_X + TOK_SPACE_CARD_PAD_COMPACT_H;   // 156
  const int iy = WEATHER_CARD_Y + TOK_SPACE_CARD_PAD_COMPACT_V;           // 216
  const int lowY = iy + WX_TEMP_DY;                                          // 267
  drawText(TOK_TYPE_NUMERAL_SM, ix, iy, STATE.weatherHigh > -900 ? String(STATE.weatherHigh) : String("--"),
           STATE.weatherHigh > -900 ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  drawText(TOK_TYPE_NUMERAL_SM, ix, lowY, STATE.weatherLow > -900 ? String(STATE.weatherLow) : String("--"),
           STATE.weatherLow > -900 ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_TERTIARY);

  drawWeatherNowAndHours(ix + 20, iy, 10);
}
