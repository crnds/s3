// sim/strip.js -- status strip, status-dot pulse and poll-progress hairline.

// ── STATUS STRIP ──────────────────────────────────────────────
const PROGRESS_Y = SCREEN_H - 1, DOT_CX = 12, WIFI_X0 = 24, GEAR_CX = 452;
const STRIP_PRESSED_R = 10;  // strip pressed states: 20px tall, the strip itself
const DOTS_W = PAGE_COUNT * 6 + (PAGE_COUNT - 1) * 6;
const CPU_X = 47;  // just past the Wi-Fi glyph (x 24..39) + an 8px gap
function drawStatusDot(r) {
  const bg = NAV.pressedId === 'health' ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_BG_CANVAS;
  if (STATE.connected) {
    gfx.fillSmoothCircle(DOT_CX, STRIP_CY, r, TOK_COLOR_STATUS_SUCCESS);
  } else {
    gfx.fillSmoothCircle(DOT_CX, STRIP_CY, r, STATE.wifiOk ? TOK_COLOR_STATUS_WARNING : TOK_COLOR_TEXT_TERTIARY);
    gfx.fillSmoothCircle(DOT_CX, STRIP_CY, r - 2, bg);
  }
}
// Status-dot pulse (pages.cpp twin): while reachable the dot's opacity
// follows a cosine 100% -> 1% -> 100% once a period, free-running.
const PULSE_BOX_R = 6;
let pulseDrawnLive = false;
function pulseLive() { return STATE.connected && !STATE.cfgReduceMotion; }
function pulseAlpha(nowMs) {
  const periodMs = Math.trunc(TOK_MOTION_PULSE_PERIOD_MS * motionTimeScale);
  const t = (Math.floor(nowMs) % periodMs) / periodMs;
  const lo = TOK_MOTION_PULSE_MIN_PCT / 100;
  return Math.trunc(255 * (lo + (1 - lo) * (0.5 + 0.5 * Math.cos(2 * Math.PI * t))));
}
function drawLiveDot(nowMs) {
  if (!pulseLive()) { drawStatusDot(4); return; }
  gfx.aaRingF(DOT_CX, STRIP_CY, 4.5, 0, TOK_COLOR_STATUS_SUCCESS, pulseAlpha(nowMs));
}
function drawWifiGlyph() {
  const c = STATE.wifiOk ? TOK_COLOR_STATUS_SUCCESS : TOK_COLOR_STATUS_ERROR;
  const bottom = STRIP_CY + 6;
  for (let i = 0; i < 3; i++) {
    const h = 4 * (i + 1), x = WIFI_X0 + 2 + i * 5;
    if (STATE.wifiOk) gfx.fillRect(x, bottom - h, 3, h, c);
    else gfx.drawRect(x, bottom - h, 3, h, c);
  }
}
function progressLineW() {
  const pollMs = effectivePollMs();
  const elapsed = Math.min(performance.now() - STATE.lastPollMs, pollMs);
  return Math.floor(elapsed / pollMs * SCREEN_W);
}
// How long one physical pixel of fill takes to appear, floored at
// TOK_MOTION_AMBIENT_TICK_MS (pages.cpp twin).
function progressStepMs() {
  const px = Math.floor(effectivePollMs() / SCREEN_W);
  return Math.max(px, TOK_MOTION_AMBIENT_TICK_MS);
}
let progressDrawnW = 0, progressLastMs = 0;
function drawStatusStrip() {
  if (NAV.pressedId === 'health') gfx.fillSmoothRoundRect(DOT_CX - 8, STRIP_CY - 10, 48, 20, 10, TOK_COLOR_FILL_PRESSED);
  drawLiveDot(performance.now());
  drawWifiGlyph();
  if (dataStale()) {
    // Stale data (design.md 13.4) takes the CPU readout's place (pages.cpp twin).
    if (STATE.lastFetchSuccessMs === 0) {
      drawText(TOK_TYPE_CAPTION, CPU_X, STRIP_CAPTION_Y, 'Saved data', TOK_COLOR_TEXT_PRIMARY);
    } else {
      const ageMin = Math.floor((performance.now() - STATE.lastFetchSuccessMs) / 60000);
      const age = ageMin < 60 ? ageMin + 'm ago' : Math.floor(ageMin / 60) + 'h ago';
      const tx = drawText(TOK_TYPE_CAPTION, CPU_X, STRIP_CAPTION_Y, 'Updated ', TOK_COLOR_TEXT_SECONDARY);
      drawText(TOK_TYPE_CAPTION, tx, STRIP_CAPTION_Y, age, TOK_COLOR_TEXT_PRIMARY);
    }
  } else {
    const cpuInt = Math.round(STATE.cpuPercentAvg);
    const cpuTx = drawText(TOK_TYPE_CAPTION, CPU_X, STRIP_CAPTION_Y, 'CPU', TOK_COLOR_TEXT_SECONDARY);
    drawText(TOK_TYPE_CAPTION, cpuTx, STRIP_CAPTION_Y, cpuInt + '%', TOK_COLOR_TEXT_PRIMARY);
  }
  let x = TOK_LAYOUT_HALF_SPLIT_X - DOTS_W / 2;
  for (let i = 0; i < PAGE_COUNT; i++, x += 12)
    gfx.fillSmoothRoundRect(x, STRIP_CY - 3, 6, 6, 3, i === STATE.currentPage ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_FILL_TRACK);
  const gp = NAV.pressedId === 'gear';
  if (gp) gfx.fillSmoothCircle(GEAR_CX, STRIP_CY, STRIP_PRESSED_R, TOK_COLOR_FILL_PRESSED);  // the 40px disc would not fit a 20px strip
  drawGearGlyph(GEAR_CX, STRIP_CY, gp ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_SECONDARY,
                gp ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_BG_CANVAS);
  progressDrawnW = 0;
  progressLastMs = performance.now();
  if (!STATE.cfgShowProgress || !STATE.connected) return;
  const w = progressLineW();
  if (w > 0) gfx.fillRect(0, PROGRESS_Y, w, 1, TOK_COLOR_TEXT_SECONDARY);
  progressDrawnW = w;
}
function progressTick(nowMs) {
  if (!STATE.cfgShowProgress || !STATE.connected) return false;
  if (nowMs - progressLastMs < progressStepMs()) return false;
  progressLastMs = nowMs;
  const w = progressLineW();
  if (w < progressDrawnW) { gfx.fillRect(0, PROGRESS_Y, SCREEN_W, 1, TOK_COLOR_BG_CANVAS); progressDrawnW = 0; }
  if (w <= progressDrawnW) return false;
  gfx.fillRect(progressDrawnW, PROGRESS_Y, w - progressDrawnW, 1, TOK_COLOR_TEXT_SECONDARY);
  progressDrawnW = w;
  return true;
}
function pulseTick(nowMs) {
  const live = pulseLive();
  if (!live && !pulseDrawnLive) return false;
  pulseDrawnLive = live;
  const b = 2 * PULSE_BOX_R + 1;
  gfx.setClipRect(DOT_CX - PULSE_BOX_R, STRIP_CY - PULSE_BOX_R, b, b);
  gfx.fillRect(DOT_CX - PULSE_BOX_R, STRIP_CY - PULSE_BOX_R, b, b, TOK_COLOR_BG_CANVAS);
  if (NAV.pressedId === 'health') gfx.fillSmoothRoundRect(DOT_CX - 8, STRIP_CY - 10, 48, 20, 10, TOK_COLOR_FILL_PRESSED);
  drawLiveDot(nowMs);
  gfx.clearClipRect();
  return true;
}
