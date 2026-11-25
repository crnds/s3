// sim/page_overview.js -- Overview page (design.md 7.5).

// ── OVERVIEW PAGE (pages.cpp twin, design.md 7.5) ─────────────
const OV_CLOCK_R = 30;
const OV_CLOCK_CX = OV_COL_X[2] + TOK_SPACE_SM + OV_CLOCK_R;   // 361
const OV_CLOCK_CY = OV_ROW_Y[0] + Math.trunc(OV_ROW_H[0] / 2);  // 52
const OV_CAL_COL = 19, OV_CAL_ROW = 14, OV_CAL_ROW6 = 13;
function drawOverviewClock(bkk) {
  drawCard(OV_COL_X[2], OV_ROW_Y[0], OV_COL_W[2], OV_ROW_H[0]);
  const tx = OV_CLOCK_CX + OV_CLOCK_R + 1 + TOK_SPACE_SM;   // 400
  const ty = OV_CLOCK_CY - Math.trunc(fontLineH(TOK_TYPE_CLOCK) / 2);
  const hour = parseInt(bkk.hour, 10), minute = parseInt(bkk.minute, 10), second = parseInt(bkk.second, 10);
  const reset = sessionResetTime(hour, minute, second);
  drawAnalogClock(OV_CLOCK_CX, OV_CLOCK_CY, OV_CLOCK_R, hour, minute, second, !!reset, reset ? reset[0] : 0, reset ? reset[1] : 0);
  drawText(TOK_TYPE_CLOCK, tx, ty, String(hour).padStart(2, '0') + ':' + String(minute).padStart(2, '0'), TOK_COLOR_TEXT_PRIMARY);
}
const MON_ABBR_LIST = ['Jan','Feb','Mar','Apr','May','Jun','Jul','Aug','Sep','Oct','Nov','Dec'];
// Date (hero readout, centred): the weekday in full (headline, accent), then
// the day in type.display with "Oct 2026" (caption) on its baseline.
function drawOverviewDate(bkk) {
  const x = OV_COL_X[0], y = OV_ROW_Y[1];
  drawCard(x, y, OV_COL_W[0], OV_ROW_H[1]);
  const tx = x + TOK_SPACE_SM;
  const numY = y + 33;
  const baseline = numY + fontAscent(TOK_TYPE_DISPLAY);
  drawText(TOK_TYPE_HEADLINE, tx, y + 12, WDAY_FULL[WDAY_ABBR.indexOf(bkk.weekday)], TOK_COLOR_ACCENT);
  const numEnd = drawText(TOK_TYPE_DISPLAY, tx, numY, String(parseInt(bkk.day, 10)), TOK_COLOR_TEXT_PRIMARY);
  drawText(TOK_TYPE_CAPTION, numEnd + TOK_SPACE_SM, baseline - fontAscent(TOK_TYPE_CAPTION), `${bkk.month} ${bkk.year}`,
           TOK_COLOR_TEXT_SECONDARY);
}
// Month calendar: the status page's month grid at 19px columns, centred.
function drawOverviewCalendar(bkk) {
  const x = OV_COL_X[1], y = OV_ROW_Y[1], w = OV_COL_W[1], h = OV_ROW_H[1];
  drawCard(x, y, w, h);
  const year = parseInt(bkk.year, 10), mon = MON_ABBR_LIST.indexOf(bkk.month);
  const day = parseInt(bkk.day, 10), wday = WDAY_ABBR.indexOf(bkk.weekday);
  const rows = monthGridRows(year, mon, day, wday);
  const gridH = rows * (rows > 5 ? OV_CAL_ROW6 : OV_CAL_ROW);
  drawMonthGrid(x + Math.trunc((w - 7 * OV_CAL_COL) / 2), y + Math.trunc((h - gridH) / 2), OV_CAL_COL, OV_CAL_ROW, OV_CAL_ROW6,
                year, mon, day, wday);
}
function drawOverviewWeather() {
  const bg = NAV.pressedId === 'weather' ? TOK_COLOR_SURFACE_RAISED : TOK_COLOR_SURFACE_CARD;
  drawCardSurface(OV_COL_X[0], OV_ROW_Y[2], OV_COL_W[0], OV_ROW_H[2], bg);
  drawWeatherNowAndHours(OV_COL_X[0] + TOK_SPACE_CARD_PAD_COMPACT_H, OV_ROW_Y[2] + TOK_SPACE_CARD_PAD_COMPACT_V, 3);
}
function drawOverviewNote() {
  const x = OV_COL_X[2], y = OV_ROW_Y[2], w = OV_COL_W[2], h = OV_ROW_H[2];
  drawCard(x, y, w, h);
  if (!STATE.note) { drawEmptyState(x + w / 2, y, h, 'No note yet', 'Edit at :8787/note', false); return; }
  drawNoteText(x + TOK_SPACE_SM, y + TOK_SPACE_SM, w - 2 * TOK_SPACE_SM, y + h - TOK_SPACE_SM, TOK_TYPE_MONO_S);
}
function drawOverviewCells() {
  const bkk = bangkokParts();
  drawLimitsColumn(true);
  drawOverviewClock(bkk);
  drawOverviewDate(bkk);
  drawOverviewCalendar(bkk);
  drawBtcCard(OV_COL_X[2], OV_ROW_Y[1], OV_COL_W[2], OV_ROW_H[1]);
  drawOverviewWeather();
  drawOverviewNote();
}
