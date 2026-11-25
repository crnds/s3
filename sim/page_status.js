// sim/page_status.js -- Status page: clock card, calendar, Thai holidays, weather card.

// ── STATUS PAGE ───────────────────────────────────────────────
const CLOCK_R = 76;
const CLOCK_CX = TOK_LAYOUT_COL_RIGHT_X + TOK_SPACE_CARD_PAD_HERO + CLOCK_R;
// Dial + digital time stack: 4 top, dial 153, 3 gap, time 28, 4 bottom = 192.
const CLOCK_CY = CLOCK_CARD_Y + 4 + CLOCK_R;
const DIGITAL_Y = CLOCK_CY + CLOCK_R + 1 + 3;
// Two cards: the clock (dial + digital time, 8px pads) and, 8px to its right,
// the calendar (date + AQI row and the month grid, centred in the card).
const CAL_COL = 20, CAL_ROW = 20, CAL_ROW_TALL = 17, CAL_DISC_R = 8;
const CLOCK_CARD_W = 2 * TOK_SPACE_CARD_PAD_HERO + 2 * CLOCK_R + 1;
const CAL_CARD_X = TOK_LAYOUT_COL_RIGHT_X + CLOCK_CARD_W + TOK_SPACE_GUTTER;
const CAL_CARD_W = TOK_LAYOUT_COL_RIGHT_X + TOK_LAYOUT_COL_RIGHT_W - CAL_CARD_X;
const CAL_GRID_X = CAL_CARD_X + Math.trunc((CAL_CARD_W - 7 * CAL_COL) / 2);
// Calendar card, top to bottom: "Tue 6" (weekday accent, date primary, both
// type.title) at the card's top on one baseline (y 35; it may run under the
// sleep / battery-save buttons, which the corner draw paints last), "Oct 2026"
// under it, a gap, the month grid, and the AQI bar pinned to the bottom pad.
// Left edge 329 is shared by the header rows and the bar.
const CAL_PAD = TOK_SPACE_SM;
const CAL_X = CAL_CARD_X + CAL_PAD;
const CAL_BAR_H = 20, CAL_BAR_W = CAL_CARD_W - 2 * CAL_PAD;
const CAL_BAR_Y = CLOCK_CARD_Y + CLOCK_CARD_H - CAL_PAD - CAL_BAR_H;
const CAL_BASE_Y = CLOCK_CARD_Y + 27;
const CAL_GRID_Y = CLOCK_CARD_Y + 56;
// Thai public holidays, (month << 8) | day per year (twin of pages.cpp's
// THAI_HOLIDAYS). Lunar dates move yearly; add new years by hand.
const THAI_HOLIDAYS = {
  2026: [0x0101,0x0303,0x0406,0x040D,0x040E,0x040F,0x0501,0x0504,0x051F,0x0603,0x071C,0x071D,0x071E,0x080C,0x0A0D,0x0A17,0x0C05,0x0C0A,0x0C1F],
  2027: [0x0101,0x0214,0x0406,0x040D,0x040E,0x040F,0x0501,0x0504,0x0514,0x0603,0x0712,0x0713,0x071C,0x080C,0x0A0D,0x0A17,0x0C05,0x0C0A,0x0C1F],
};
function isThaiHoliday(year, mon, d) {
  const t = THAI_HOLIDAYS[year];
  return !!t && t.includes(((mon + 1) << 8) | d);
}
// Minimal month grid (twin of pages.cpp's drawMonthGrid).
// Calendar-card footer: full-width bar in the AQI colour, "AQI" left and the
// index right-aligned (a category word does not fit 135px). aqi < 0 draws a
// neutral placeholder.
function drawAqiBar(x, y, w, aqi) {
  let bg = TOK_COLOR_FILL_TRACK, fg = TOK_COLOR_TEXT_TERTIARY;
  if (aqi >= 0) [bg, fg] = aqiColors(aqi);
  gfx.fillSmoothRoundRect(x, y, w, CAL_BAR_H, TOK_RADIUS_SM, bg);
  const ty = y + Math.trunc((CAL_BAR_H - fontLineH(TOK_TYPE_CAPTION)) / 2);
  drawText(TOK_TYPE_LABEL, x + TOK_SPACE_SM, ty, 'AQI', fg);
  if (aqi < 0) { drawTextR(TOK_TYPE_LABEL, x + w - TOK_SPACE_SM, ty, '--', fg); return; }
  drawTextR(TOK_TYPE_LABEL, x + w - TOK_SPACE_SM, ty, String(aqi), fg);
}
function daysInMonth(m, yr) { return new Date(Date.UTC(yr, m + 1, 0)).getUTCDate(); }
// One calendar day centred on (cx, cy): day d of month `mon` (d < 1 or past
// the month's end = a neighbouring month's day), in weekday column c.
function drawCalendarDay(cx, cy, year, mon, d, c, mday) {
  const dim = daysInMonth(mon, year);
  const prevDim = mon === 0 ? daysInMonth(11, year - 1) : daysInMonth(mon - 1, year);
  const other = d < 1 || d > dim;
  const shown = d < 1 ? prevDim + d : d > dim ? d - dim : d;
  let col = other ? TOK_COLOR_TEXT_TERTIARY
          : isThaiHoliday(year, mon, d) ? TOK_COLOR_HOLIDAY
          : (c === 0 || c === 6) ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_PRIMARY;
  if (!other && d === mday) {
    gfx.fillSmoothCircle(cx, cy, CAL_DISC_R, TOK_COLOR_ACCENT);
    col = TOK_COLOR_TEXT_PRIMARY;
  }
  drawTextC(TOK_TYPE_CALENDAR, cx, cy - Math.trunc(fontLineH(TOK_TYPE_CALENDAR) / 2), String(shown), col);
}
function firstWeekdayOfMonth(mday, wday) { return (((wday - (mday - 1)) % 7) + 7) % 7; }
function monthGridRows(year, mon, mday, wday) {
  return Math.trunc((firstWeekdayOfMonth(mday, wday) + daysInMonth(mon, year) + 6) / 7);
}
// colW-wide columns; rows rowH tall, or rowH6 in a 6-row month.
function drawMonthGrid(x, y, colW, rowH, rowH6, year, mon, mday, wday) {
  const first = firstWeekdayOfMonth(mday, wday);
  const rows = monthGridRows(year, mon, mday, wday);
  if (rows > 5) rowH = rowH6;
  for (let r = 0; r < rows; r++)
    for (let c = 0; c < 7; c++)
      drawCalendarDay(x + c * colW + Math.trunc(colW / 2), y + r * rowH + Math.trunc(rowH / 2), year, mon, r * 7 + c - first + 1, c, mday);
}
// Forecast columns (the status strip, the Overview's weather cell) from the
// label row's top iy: label caption 17, the glyph centred 34 down, the
// temperature 51 down, 68 in all.
const WX_GLYPH_DY = TOK_LINE_CAPTION + 6 + 11, WX_TEMP_DY = TOK_LINE_CAPTION + 12 + 22, WX_ROWS_H = WX_TEMP_DY + TOK_LINE_CAPTION;
// "Now" (a 44px column from x) and, past a 1px divider, the next `hours` hours.
function drawWeatherNowAndHours(x, iy, hours) {
  const nowCx = x + 22;
  drawTextC(TOK_TYPE_CAPTION, nowCx, iy, 'Now', TOK_COLOR_ACCENT);
  drawWeatherIcon(nowCx, iy + WX_GLYPH_DY, STATE.weatherCode, 1.2);
  drawTemp(TOK_TYPE_NUMERAL_SM, nowCx, iy + WX_TEMP_DY, Math.round(STATE.weatherTempC), STATE.weatherTempC > -900);
  gfx.fillRect(x + 44, iy, 1, WX_ROWS_H, TOK_COLOR_TEXT_TERTIARY);
  for (let i = 0; i < hours; i++) {
    const idx = i + 1;
    const cx = x + 44 + i * 24 + 12;
    const have = STATE.weatherHourly.length > idx;
    const hr = have ? STATE.weatherHourly[idx] : null;
    drawTextC(TOK_TYPE_NUMERAL_SM, cx, iy, have ? String(hr.h).padStart(2, '0') : '--',
              have ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_TERTIARY);
    drawWeatherIcon(cx, iy + WX_GLYPH_DY, have ? hr.code : -1, 0.9);
    drawTemp(TOK_TYPE_NUMERAL_SM, cx, iy + WX_TEMP_DY, have ? hr.tempC : 0, have);
  }
}
// The 5H window's reset as a wall-clock [hour, minute], or null.
function sessionResetTime(hour, minute, second) {
  const rem = liveResetsInSec(STATE.sessionResetsInSec);
  if (rem < 0) return null;
  const totalSec = (hour * 3600 + minute * 60 + second + rem) % 86400;
  return [Math.floor(totalSec / 3600), Math.floor((totalSec % 3600) / 60)];
}
function drawStatusPage() {
  drawLimitsColumn();
  drawBtcCard();
  drawCard(TOK_LAYOUT_COL_RIGHT_X, CLOCK_CARD_Y, CLOCK_CARD_W, CLOCK_CARD_H);
  drawCard(CAL_CARD_X, CLOCK_CARD_Y, CAL_CARD_W, CLOCK_CARD_H);
  const wxBg = NAV.pressedId === 'weather' ? TOK_COLOR_SURFACE_RAISED : TOK_COLOR_SURFACE_CARD;
  drawCardSurface(TOK_LAYOUT_COL_RIGHT_X, WEATHER_CARD_Y, TOK_LAYOUT_COL_RIGHT_W, WEATHER_CARD_H, wxBg);

  const bkk = bangkokParts();
  const hour = parseInt(bkk.hour, 10), minute = parseInt(bkk.minute, 10), second = parseInt(bkk.second, 10);
  const reset = sessionResetTime(hour, minute, second);
  drawAnalogClock(CLOCK_CX, CLOCK_CY, CLOCK_R, hour, minute, second, !!reset, reset ? reset[0] : 0, reset ? reset[1] : 0);

  // Digital time (type.clock) centred under the dial.
  drawTextC(TOK_TYPE_CLOCK, CLOCK_CX, DIGITAL_Y, String(hour).padStart(2, '0') + ':' + String(minute).padStart(2, '0'), TOK_COLOR_TEXT_PRIMARY);

  // Calendar card: "Tue 6" (type.title, weekday accent) on one baseline, "Oct 2026"
  // under it, the month grid, and the AQI bar in the bottom pad.
  const numY = CAL_BASE_Y - fontAscent(TOK_TYPE_TITLE);
  const wdEnd = drawText(TOK_TYPE_TITLE, CAL_X, numY, bkk.weekday, TOK_COLOR_ACCENT);
  drawText(TOK_TYPE_TITLE, wdEnd + TOK_SPACE_SM, numY, String(parseInt(bkk.day, 10)), TOK_COLOR_TEXT_PRIMARY);
  drawText(TOK_TYPE_CAPTION, CAL_X, CAL_BASE_Y + 1, `${bkk.month} ${bkk.year}`, TOK_COLOR_TEXT_SECONDARY);
  // 6-row months squeeze to 17px rows so the grid stays ~100px tall.
  drawMonthGrid(CAL_GRID_X, CAL_GRID_Y, CAL_COL, CAL_ROW, CAL_ROW_TALL, parseInt(bkk.year, 10),
                ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'].indexOf(bkk.month),
                parseInt(bkk.day, 10), WDAY_ABBR.indexOf(bkk.weekday));
  if (STATE.cfgShowAqi) drawAqiBar(CAL_X, CAL_BAR_Y, CAL_BAR_W, STATE.aqi);

  const ix = TOK_LAYOUT_COL_RIGHT_X + TOK_SPACE_CARD_PAD_COMPACT_H;
  const iy = WEATHER_CARD_Y + TOK_SPACE_CARD_PAD_COMPACT_V;
  const lowY = iy + WX_TEMP_DY;
  drawText(TOK_TYPE_NUMERAL_SM, ix, iy, STATE.weatherHigh > -900 ? String(STATE.weatherHigh) : '--',
           STATE.weatherHigh > -900 ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_TERTIARY);
  drawText(TOK_TYPE_NUMERAL_SM, ix, lowY, STATE.weatherLow > -900 ? String(STATE.weatherLow) : '--',
           STATE.weatherLow > -900 ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_TERTIARY);
  drawWeatherNowAndHours(ix + 20, iy, 10);
}
