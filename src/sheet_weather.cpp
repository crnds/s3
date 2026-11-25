// Weather sheet.
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"

// ── WEATHER SHEET ──────────────────────────────────────────
// No close glyph -- a tap anywhere dismisses it -- so the hero card spans the
// full content width: x 8..471, y 8..71. Then the hourly card (now + 10) and the
// 5-day card.
static const int WX_HERO_X = TOK_LAYOUT_CONTENT_X0, WX_HERO_Y = TOK_LAYOUT_CONTENT_Y0;
static const int WX_HERO_W = TOK_LAYOUT_CONTENT_X1 - WX_HERO_X, WX_HERO_H = 64;
static const int WX_HOURLY_Y = WX_HERO_Y + WX_HERO_H + TOK_SPACE_GUTTER;          // 80
static const int WX_HOURLY_H = TOK_LINE_CAPTION + TOK_SPACE_XS + 28 + TOK_SPACE_XS + TOK_LINE_BODY + 2 * TOK_SPACE_CARD_PAD_COMPACT_V;  // 92
static const int WX_DAILY_Y = WX_HOURLY_Y + WX_HOURLY_H + TOK_SPACE_GUTTER;       // 180
static const int WX_DAILY_H = TOK_LAYOUT_OVERLAY_CONTENT_Y1 - WX_DAILY_Y;          // 132

void drawWeatherPage() {
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
  const int metaY = WX_HERO_Y + (WX_HERO_H - (TOK_LINE_BODY + TOK_SPACE_XS + TOK_LINE_BODY)) / 2;
  {
    const char* cond = STATE.weatherCondition[0] ? STATE.weatherCondition : "--";
    drawText(TOK_TYPE_HEADLINE, metaX, metaY, fitText(TOK_TYPE_HEADLINE, cond, metaMaxX - metaX),
             STATE.weatherCondition[0] ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  }
  {
    const int y = metaY + TOK_LINE_BODY + TOK_SPACE_XS;
    int x = drawText(TOK_TYPE_BODY, metaX, y, "H ", TOK_COLOR_TEXT_SECONDARY);
    if (STATE.weatherHigh > -900) x = drawText(TOK_TYPE_BODY, x, y, String(STATE.weatherHigh), TOK_COLOR_TEXT_PRIMARY);
    else x = drawText(TOK_TYPE_BODY, x, y, "--", TOK_COLOR_TEXT_TERTIARY);
    x = drawText(TOK_TYPE_BODY, x + TOK_SPACE_LG, y, "L ", TOK_COLOR_TEXT_SECONDARY);
    if (STATE.weatherLow > -900) drawText(TOK_TYPE_BODY, x, y, String(STATE.weatherLow), TOK_COLOR_TEXT_PRIMARY);
    else drawText(TOK_TYPE_BODY, x, y, "--", TOK_COLOR_TEXT_TERTIARY);
  }
  if (haveAqi) drawAqiBadge(badgeX, WX_HERO_Y + (WX_HERO_H - TOK_BADGE_H) / 2, STATE.aqi);

  // ── Hourly (now + 10): hour (caption), glyph.content.md, temperature (body) ──
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
    drawWeatherIcon(cx, hy + TOK_LINE_CAPTION + TOK_SPACE_XS + 14, have ? STATE.weatherHourly[i].code : -1, 1.5f);
    const int ty = hy + TOK_LINE_CAPTION + TOK_SPACE_XS + 28 + TOK_SPACE_XS;
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
  const int barX = TOK_LAYOUT_ALIGN_X - 36, barW = 260, highX = barX + barW + TOK_SPACE_SM;
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
      aaFillRoundRect(x0, barY, x1 - x0, TOK_METER_MD, TOK_METER_MD / 2, TOK_COLOR_ACCENT);
    }
    if (have) drawText(TOK_TYPE_BODY, highX, y, String(hi), TOK_COLOR_TEXT_PRIMARY);
    else drawText(TOK_TYPE_BODY, highX, y, "--", TOK_COLOR_TEXT_TERTIARY);
  }
}
