// sim/page_usage.js -- Usage page: limits column rows, projects and trend.

// ── USAGE PAGE (page 1) ───────────────────────────────────────
// The old Projects and Limits pages on one page, in the Status page's grid:
// a limits column (x 8..135) and, right (x 144..471), top projects over the
// 7-day trend (pages.cpp's drawUsagePage).
const PROJ_CARD_H = 171, TREND_CARD_H = 105;
const TREND_CARD_Y = TOK_LAYOUT_CONTENT_Y0 + PROJ_CARD_H + TOK_SPACE_GUTTER;
const PROJ_ROW_STEP = 23 + TOK_SPACE_STACK, PROJ_VALUE_W = 56;
const PROJ_NAME_W = 116;                    // project name column (fitText)
const TREND_BAR_W = 20, TREND_BAR_GAP = 4;  // 7 bars = 164 px from the align line
function drawEmptyState(cx, y0, h, title, desc, isError, titleFont = TOK_TYPE_HEADLINE, titleColor = TOK_COLOR_TEXT_PRIMARY) {
  const th = fontLineH(titleFont);
  const total = th + TOK_SPACE_SM + 17;
  const y = y0 + Math.trunc((h - total) / 2);
  drawTextC(titleFont, cx, y, title, isError ? TOK_COLOR_STATUS_ERROR : titleColor);
  drawTextC(TOK_TYPE_CAPTION, cx, y + th + TOK_SPACE_SM, desc, TOK_COLOR_TEXT_SECONDARY);
}
function drawUsagePage() {
  // ── limits column ──
  const lx0 = TOK_LAYOUT_COL_LEFT_X, lw = TOK_LAYOUT_COL_LEFT_W;
  drawCard(lx0, TOK_LAYOUT_CONTENT_Y0, lw, TOK_LAYOUT_CONTENT_Y1 - TOK_LAYOUT_CONTENT_Y0);
  const ix = lx0 + TOK_SPACE_SM, ir = lx0 + lw - TOK_SPACE_SM;  // 16..128: 112 inner
  const ltop = TOK_LAYOUT_CONTENT_Y0 + TOK_SPACE_CARD_PAD;
  drawSectionLabel(ix, ltop, 'LIMITS');
  let ly = ltop + 17 + TOK_SPACE_SM;
  drawLimitRow(ix, ir, ly, 'Context', STATE.ctxTokens >= 0 ? STATE.ctxPercent : -1);
  ly += ROW_STEP;
  drawLimitRow(ix, ir, ly, '5-hour', STATE.sessionPercent);
  ly += ROW_STEP;
  drawLimitRow(ix, ir, ly, 'Weekly', STATE.weekPercent);
  ly += ROW_STEP;
  if (STATE.weekModelPercent >= 0) {
    drawLimitRow(ix, ir, ly, STATE.weekModelName || 'Model', STATE.weekModelPercent);
    ly += ROW_STEP;
  }
  if (STATE.creditsUsed >= 0) drawLimitRow(ix, ir, ly, 'Credits', STATE.creditsPercent);

  // ── right column: top projects ──
  const x0 = TOK_LAYOUT_COL_RIGHT_X, w = TOK_LAYOUT_COL_RIGHT_W;
  drawCard(x0, TOK_LAYOUT_CONTENT_Y0, w, PROJ_CARD_H);
  const px = x0 + TOK_SPACE_CARD_PAD, pr = x0 + w - TOK_SPACE_CARD_PAD;  // 156..460
  const alignX = px + PROJ_NAME_W + TOK_SPACE_SM;  // meters and trend bars share this line
  const top = TOK_LAYOUT_CONTENT_Y0 + TOK_SPACE_CARD_PAD;
  drawSectionLabel(px, top, 'TOP PROJECTS 7D');
  if (STATE.projects.length === 0) {
    const ey = top + 17;
    drawEmptyState(x0 + w / 2, ey, TOK_LAYOUT_CONTENT_Y0 + PROJ_CARD_H - ey, 'No project data yet',
                   'Projects appear after the first poll', false);
  } else {
    const shown = Math.min(STATE.projects.length, 4);
    let maxTokens = 1;
    for (let i = 0; i < shown; i++) maxTokens = Math.max(maxTokens, STATE.projects[i].tokens);
    const meterW = pr - PROJ_VALUE_W - TOK_SPACE_SM - alignX;
    let y = top + 17 + TOK_SPACE_SM;
    for (let i = 0; i < shown; i++, y += PROJ_ROW_STEP) {
      const p = STATE.projects[i];
      drawText(TOK_TYPE_BODY, px, y, fitText(TOK_TYPE_BODY, p.name, PROJ_NAME_W), TOK_COLOR_TEXT_PRIMARY);
      const pct = Math.floor(p.tokens / maxTokens * 100 + 0.5);
      drawMeter(alignX, y + 12 - TOK_METER_MD / 2, meterW, TOK_METER_MD, pct, TOK_COLOR_DATA_USAGE);
      drawTextR(TOK_TYPE_NUMERAL_SM, pr, y + fontAscent(TOK_TYPE_BODY) - fontAscent(TOK_TYPE_CAPTION),
                fmtTokens(p.tokens), TOK_COLOR_TEXT_SECONDARY);
    }
  }

  // ── right column: 7-day trend (compact card): label column left, bars from alignX ──
  drawCard(x0, TREND_CARD_Y, w, TREND_CARD_H);
  const ty = TREND_CARD_Y + TOK_SPACE_CARD_PAD_COMPACT_V;
  drawSectionLabel(px, ty, '7-DAY TREND');
  let total = 0, maxTrend = 1;
  for (let i = 0; i < 7; i++) { total += STATE.trend[i]; maxTrend = Math.max(maxTrend, STATE.trend[i]); }
  drawText(TOK_TYPE_BODY, px, ty + 17 + TOK_SPACE_SM, fmtTokens(total), TOK_COLOR_TEXT_PRIMARY);
  drawText(TOK_TYPE_CAPTION, px, ty + 17 + TOK_SPACE_SM + 23, 'this week', TOK_COLOR_TEXT_SECONDARY);
  const DAY_LABELS = ['-6', '-5', '-4', '-3', '-2', '-1', 'Now'];
  const barW = TREND_BAR_W, gap = TREND_BAR_GAP;
  const axisY = TREND_CARD_Y + TREND_CARD_H - TOK_SPACE_CARD_PAD_COMPACT_V - 17;
  const feet = axisY - TOK_SPACE_XS;  // bars end above the axis labels
  const chartH = feet - ty;
  for (let i = 0; i < 7; i++) {
    const bx = alignX + i * (barW + gap);
    let bh = Math.trunc(STATE.trend[i] / maxTrend * chartH);
    if (bh < 4) bh = 4;
    gfx.fillSmoothRoundRect(bx, feet - bh, barW, bh, TOK_RADIUS_SM, TOK_COLOR_DATA_USAGE);
    drawTextC(TOK_TYPE_CAPTION, bx + Math.trunc(barW / 2), axisY, DAY_LABELS[i], i === 6 ? TOK_COLOR_ACCENT : TOK_COLOR_TEXT_TERTIARY);
  }
}
