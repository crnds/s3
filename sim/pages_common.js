// sim/pages_common.js -- shared drawing: primitives, glyphs, corner, weather icons, pace sweep, left column, clock.

// ── PRIMITIVES (pages.cpp twin) ───────────────────────────────
function drawCardSurface(x, y, w, h, fill) {
  gfx.fillSmoothRoundRect(x, y, w, h, TOK_RADIUS_MD, fill);
  if (TOK_CARD_OUTLINE) gfx.drawRoundRect(x, y, w, h, TOK_RADIUS_MD, TOK_COLOR_CARD_OUTLINE);
}
function drawCard(x, y, w, h) { drawCardSurface(x, y, w, h, TOK_COLOR_SURFACE_CARD); }
function drawSectionLabel(x, y, label) { drawText(TOK_TYPE_LABEL, x, y, label, TOK_COLOR_TEXT_SECONDARY); }
function drawMeter(x, y, w, h, percent, color) {
  const r = h >> 1;
  gfx.fillSmoothRoundRect(x, y, w, h, r, TOK_COLOR_FILL_TRACK);
  if (percent < 0) return -1;
  let fillW = Math.floor(Math.min(percent, 100) / 100 * w + 0.5);
  if (fillW < h) fillW = h;
  gfx.fillSmoothRoundRect(x, y, fillW, h, r, color);
  return fillW;
}
function fitText(f, s, maxW) {
  if (textW(f, s) <= maxW) return s;
  let out = s;
  while (out.length > 0 && textW(f, out) > maxW) out = out.slice(0, -1);
  const sp = out.lastIndexOf(' ');
  if (sp > 0 && out.length - sp <= 4) out = out.slice(0, sp);
  return out;
}

// ── SYSTEM GLYPHS ─────────────────────────────────────────────
function drawCloseGlyph(cx, cy, c) {
  gfx.drawWideLine(cx - 6, cy - 6, cx + 6, cy + 6, TOK_STROKE_GLYPH_R, c);
  gfx.drawWideLine(cx + 6, cy - 6, cx - 6, cy + 6, TOK_STROKE_GLYPH_R, c);
}
function drawBackGlyph(cx, cy, c) {
  gfx.drawWideLine(cx + 4, cy - 7, cx - 4, cy, TOK_STROKE_GLYPH_R, c);
  gfx.drawWideLine(cx - 4, cy, cx + 4, cy + 7, TOK_STROKE_GLYPH_R, c);
}
function drawChevron(cx, cy, c) {
  gfx.drawWideLine(cx - 2, cy - 5, cx + 3, cy, TOK_STROKE_GLYPH_R, c);
  gfx.drawWideLine(cx + 3, cy, cx - 2, cy + 5, TOK_STROKE_GLYPH_R, c);
}
function drawGearGlyph(cx, cy, c, bg) {
  gfx.fillSmoothCircle(cx, cy, 6, c);
  gfx.fillSmoothCircle(cx, cy, 4, bg);
  for (let i = 0; i < 8; i++) {
    const a = i * Math.PI / 4;
    gfx.drawWideLine(cx + Math.round(Math.cos(a) * 7), cy + Math.round(Math.sin(a) * 7),
                     cx + Math.round(Math.cos(a) * 9), cy + Math.round(Math.sin(a) * 9), TOK_STROKE_GLYPH_R, c);
  }
}
function drawIconButtonPressed(cx, cy) { gfx.fillSmoothCircle(cx, cy, TOK_PRESSED_DISC_R, TOK_COLOR_FILL_PRESSED); }
function drawShuffleButton(cx, cy, pressed) {
  gfx.fillSmoothCircle(cx, cy, SHUFFLE_DISC_R, pressed ? TOK_COLOR_FILL_PRESSED : TOK_COLOR_PLATE);
  const c = TOK_COLOR_TEXT_PRIMARY, r = TOK_STROKE_GLYPH_R;
  gfx.drawWideLine(cx - 7, cy - 4, cx - 3, cy - 4, r, c);
  gfx.drawWideLine(cx - 3, cy - 4, cx + 3, cy + 4, r, c);
  gfx.drawWideLine(cx + 3, cy + 4, cx + 7, cy + 4, r, c);
  gfx.drawWideLine(cx - 7, cy + 4, cx - 3, cy + 4, r, c);
  gfx.drawWideLine(cx - 3, cy + 4, cx + 3, cy - 4, r, c);
  gfx.drawWideLine(cx + 3, cy - 4, cx + 7, cy - 4, r, c);
  gfx.fillTriangle(cx + 8, cy - 4, cx + 5, cy - 7, cx + 5, cy - 1, c);
  gfx.fillTriangle(cx + 8, cy + 4, cx + 5, cy + 1, cx + 5, cy + 7, c);
}
function shuffleCentre(offline) {
  // 8px in from the media box's bottom-right: the full screen, or the pane.
  let x1 = SCREEN_W, y1 = SCREEN_H;
  if (catPanePage(offline)) {
    const [px, py, pw, ph] = catPaneRect();
    x1 = px + pw;
    y1 = py + ph;
  }
  return [x1 - TOK_SPACE_SM - SHUFFLE_DISC_R - 1, y1 - TOK_SPACE_SM - SHUFFLE_DISC_R - 1];
}

// ── SYSTEM CORNER ─────────────────────────────────────────────
function drawSystemCorner(overMedia) {
  const ay = TOK_CORNER_SLOT_Y, sz = TOK_CORNER_SLOT_SIZE;
  if (batterySaveActive()) {
    const bx = TOK_CORNER_SLOT_B_X;
    if (overMedia) gfx.fillRect(bx - 4, ay - 4, sz + 8, sz + 8, TOK_COLOR_PLATE);
    const c = TOK_COLOR_STATUS_WARNING, x = bx + 3, y = ay + 7;
    gfx.drawRoundRect(x, y, 16, 10, 2, c);
    gfx.drawRoundRect(x + 1, y + 1, 14, 8, 1, c);
    gfx.fillRect(x + 3, y + 3, 5, 4, c);
    gfx.fillRect(x + 16, y + 3, 2, 4, c);
  }
  const cx = TOK_CORNER_SLOT_A_X + sz / 2, cy = ay + sz / 2;
  const pressed = NAV.pressedId === 'sleep';
  gfx.fillSmoothRoundRect(cx - 7, cy - 3, 14, 6, 3, pressed ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_SECONDARY);
}

function drawModalHeader(back, title, pressed) {
  const cx = TOK_CLOSE_SLOT_X + 12, cy = TOK_CLOSE_SLOT_Y + 12;
  if (pressed) drawIconButtonPressed(cx, cy);
  const c = pressed ? TOK_COLOR_TEXT_PRIMARY : TOK_COLOR_TEXT_SECONDARY;
  if (back) drawBackGlyph(cx, cy, c); else drawCloseGlyph(cx, cy, c);
  if (title) drawText(TOK_TYPE_HEADLINE, TOK_HEADER_TITLE_X, TOK_HEADER_TITLE_Y, title, TOK_COLOR_TEXT_PRIMARY);
}

// ── DATA ROWS (Usage page limits column, Device page) ────────
const ROW_H = 23 + TOK_SPACE_STACK_TIGHT + TOK_METER_MD, ROW_STEP = ROW_H + TOK_SPACE_STACK;
// A limits-column row in 112 px: label (body) left, percent (numeral.sm, on the
// label's baseline) right, meter.md under it. No detail text -- the Status
// page's 5H / Week cards carry the "Resets ..." lines. numeral.sm, not .md,
// because "Context" + "100%" at .md is wider than the column.
function drawLimitRow(x, r, y, label, percent) {
  const pct = percent >= 0 ? percent + '%' : '--';
  const pw = textW(TOK_TYPE_NUMERAL_SM, pct);
  drawText(TOK_TYPE_BODY, x, y, fitText(TOK_TYPE_BODY, label, r - x - pw - TOK_SPACE_XS), TOK_COLOR_TEXT_PRIMARY);
  drawTextR(TOK_TYPE_NUMERAL_SM, r, y + fontAscent(TOK_TYPE_BODY) - fontAscent(TOK_TYPE_CAPTION), pct,
            percent < 0 ? TOK_COLOR_TEXT_TERTIARY : dataStale() ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_TEXT_PRIMARY);
  drawMeter(x, y + 23 + TOK_SPACE_STACK_TIGHT, r - x, TOK_METER_MD, percent, TOK_COLOR_DATA_USAGE);
}

// ── WEATHER GLYPHS ────────────────────────────────────────────
function weatherIconColour(code) {
  if (code <= 1 || code >= 95) return TOK_COLOR_CONTENT_SUN;
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) return TOK_COLOR_CONTENT_SNOW;
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) return TOK_COLOR_CONTENT_RAIN;
  return TOK_COLOR_CONTENT_CLOUD;
}
function weatherIconShape(cx, cy, code, k, c) {
  const S = (v) => Math.round(v * k);
  const lw = 1.0 * k;
  if (code === 0 || code === 1) {
    gfx.fillSmoothCircle(cx, cy, S(4), c);
    gfx.drawWideLine(cx, cy - S(9), cx, cy - S(7), lw, c);
    gfx.drawWideLine(cx, cy + S(7), cx, cy + S(9), lw, c);
    gfx.drawWideLine(cx - S(9), cy, cx - S(7), cy, lw, c);
    gfx.drawWideLine(cx + S(7), cy, cx + S(9), cy, lw, c);
    gfx.drawWideLine(cx - S(7), cy - S(7), cx - S(5), cy - S(5), lw, c);
    gfx.drawWideLine(cx + S(5), cy + S(5), cx + S(7), cy + S(7), lw, c);
    gfx.drawWideLine(cx - S(7), cy + S(7), cx - S(5), cy + S(5), lw, c);
    gfx.drawWideLine(cx + S(5), cy - S(5), cx + S(7), cy - S(7), lw, c);
    return;
  }
  if (code >= 95) {
    gfx.fillTriangle(cx - S(5), cy + S(2), cx + S(1), cy - S(8), cx + S(1), cy - S(2), c);
    gfx.fillTriangle(cx - S(5), cy + S(2), cx + S(1), cy - S(2), cx - S(1), cy + S(2), c);
    gfx.fillTriangle(cx - S(1), cy + S(2), cx + S(1), cy - S(2), cx + S(5), cy - S(2), c);
    gfx.fillTriangle(cx - S(1), cy + S(2), cx + S(5), cy - S(2), cx - S(1), cy + S(8), c);
    return;
  }
  if ((code >= 71 && code <= 77) || code === 85 || code === 86) {
    gfx.drawWideLine(cx, cy - S(7), cx, cy + S(7), lw, c);
    gfx.drawWideLine(cx - S(6), cy - S(4), cx + S(6), cy + S(4), lw, c);
    gfx.drawWideLine(cx - S(6), cy + S(4), cx + S(6), cy - S(4), lw, c);
    return;
  }
  if ((code >= 51 && code <= 67) || (code >= 80 && code <= 82)) {
    gfx.fillTriangle(cx, cy - S(8), cx - S(4.45), cy + S(0.7), cx + S(4.45), cy + S(0.7), c);
    gfx.fillSmoothCircle(cx, cy + S(3), S(5), c);
    return;
  }
  gfx.fillSmoothCircle(cx - S(4), cy - S(2), S(4), c);
  gfx.fillSmoothCircle(cx + S(3), cy - S(3), S(5), c);
  gfx.fillSmoothRoundRect(cx - S(9), cy - S(2), S(19), S(9), S(4), c);
}

// Weather glyph material (design.md 10.5): pages.cpp's compositeWeatherIcon,
// same integer math. The shape renders white-on-black into a 64x64 mask; the
// shadow, gradient, bevel and specular spot all come from that coverage.
const WX_MASK = 64, WX_MASK_C = WX_MASK / 2;
const wxMaskCanvas = document.createElement('canvas');
wxMaskCanvas.width = wxMaskCanvas.height = WX_MASK;
const wxMaskCtx = wxMaskCanvas.getContext('2d', { willReadFrequently: true });
const wxCov = new Uint8Array(WX_MASK * WX_MASK);
let wxX0 = 0, wxY0 = 0, wxX1 = -1, wxY1 = -1;  // coverage bbox in the mask, inclusive
function covAt(x, y) { return (x < 0 || y < 0 || x >= WX_MASK || y >= WX_MASK) ? 0 : wxCov[y * WX_MASK + x]; }
const lighten = (v, amt) => v + (((255 - v) * amt) >> 8);
const darken = (v, amt) => v - ((v * amt) >> 8);
function wxShade(mx, my, r9, ch) {
  let t = Math.trunc((my - WX_MASK_C) * 256 / r9);
  if (t > 256) t = 256;
  if (t < -256) t = -256;
  const e = covAt(mx - 1, my - 1) - covAt(mx + 1, my + 1);
  const u = Math.trunc((mx - WX_MASK_C) * 256 / r9) + 90, v = Math.trunc((my - WX_MASK_C) * 256 / r9) + 115;
  const spot = Math.trunc(u * u / 52) + Math.trunc(v * v / 23);
  for (let i = 0; i < 3; i++) {
    let c = ch[i];
    c = t < 0 ? lighten(c, (-t * 56) >> 8) : darken(c, (t * 64) >> 8);
    c = e < 0 ? lighten(c, (-e * 110) >> 8) : darken(c, (e * 90) >> 8);
    if (spot < 256) c = lighten(c, ((256 - spot) * 130) >> 8);
    ch[i] = c;
  }
}
function compositeWeatherIcon(cx, cy, k, base) {
  const r9 = Math.round(9 * k);
  const sdx = Math.max(1, Math.round(0.6 * k)), sdy = Math.max(1, Math.round(1.2 * k));
  const m = /rgb\((\d+),(\d+),(\d+)\)/.exec(base);
  const b0 = [+m[1], +m[2], +m[3]];
  const ox = cx - WX_MASK_C, oy = cy - WX_MASK_C;
  const img = ctx.getImageData(ox, oy, WX_MASK, WX_MASK), d = img.data;
  const ch = [0, 0, 0];
  const x0 = Math.max(0, wxX0 - 1), y0 = Math.max(0, wxY0 - 1);
  const x1 = Math.min(WX_MASK - 1, wxX1 + sdx + 1), y1 = Math.min(WX_MASK - 1, wxY1 + sdy + 1);
  for (let my = y0; my <= y1; my++) {
    for (let mx = x0; mx <= x1; mx++) {
      const cov = wxCov[my * WX_MASK + mx];
      let sh = 0;
      for (let j = -1; j <= 1; j++) for (let i = -1; i <= 1; i++) sh += covAt(mx - sdx + i, my - sdy + j);
      sh = Math.trunc(sh / 9);
      if (!cov && !sh) continue;
      const p = (my * WX_MASK + mx) * 4;
      let dr = d[p], dg = d[p + 1], db = d[p + 2];
      if (sh) {
        const a = (sh * 140) >> 8;
        dr = darken(dr, a); dg = darken(dg, a); db = darken(db, a);
      }
      if (cov) {
        ch[0] = b0[0]; ch[1] = b0[1]; ch[2] = b0[2];
        wxShade(mx, my, r9, ch);
        dr += Math.trunc((ch[0] - dr) * cov / 255);
        dg += Math.trunc((ch[1] - dg) * cov / 255);
        db += Math.trunc((ch[2] - db) * cov / 255);
      }
      d[p] = dr; d[p + 1] = dg; d[p + 2] = db;
    }
  }
  ctx.putImageData(img, ox, oy);
}
function drawWeatherIcon(cx, cy, code, k) {
  if (code < 0) { drawTextC(TOK_TYPE_CAPTION, cx, cy - Math.trunc(fontLineH(TOK_TYPE_CAPTION) / 2), '--', TOK_COLOR_TEXT_TERTIARY); return; }
  const saved = ctx;
  wxMaskCtx.fillStyle = 'rgb(0,0,0)';
  wxMaskCtx.fillRect(0, 0, WX_MASK, WX_MASK);
  ctx = wxMaskCtx;
  // Additive, so the canvas's AA seams where two parts meet sum back to solid
  // (the firmware's fillTriangle has no AA, so it has no seams to close).
  wxMaskCtx.globalCompositeOperation = 'lighter';
  weatherIconShape(WX_MASK_C, WX_MASK_C, code, k, 'rgb(255,255,255)');
  wxMaskCtx.globalCompositeOperation = 'source-over';
  ctx = saved;
  const md = wxMaskCtx.getImageData(0, 0, WX_MASK, WX_MASK).data;
  wxX0 = wxY0 = WX_MASK; wxX1 = wxY1 = -1;
  for (let i = 0; i < WX_MASK * WX_MASK; i++) {
    wxCov[i] = md[i * 4 + 1];
    if (wxCov[i]) {
      const x = i % WX_MASK, y = Math.trunc(i / WX_MASK);
      wxX0 = Math.min(wxX0, x); wxX1 = Math.max(wxX1, x); wxY0 = Math.min(wxY0, y); wxY1 = Math.max(wxY1, y);
    }
  }
  if (wxX1 < 0) return;
  compositeWeatherIcon(cx, cy, k, weatherIconColour(code));
}
function drawDegreeRing(x, capTop, bg) {
  gfx.fillSmoothCircle(x + 4, capTop + 4, 4, TOK_COLOR_TEXT_SECONDARY);
  gfx.fillSmoothCircle(x + 4, capTop + 4, 2, bg);
  return x + 9;
}
function drawTemp(f, cx, y, t, have) {
  if (have) drawTextC(f, cx, y, String(t), TOK_COLOR_TEXT_PRIMARY);
  else drawTextC(f, cx, y, '--', TOK_COLOR_TEXT_TERTIARY);
}

// ── PACE SWEEP (one per successful poll) ──────────────────────
const SHINE_BAND_R = 10;
const SHINE_BAR_X = TOK_LAYOUT_COL_LEFT_X + TOK_SPACE_SM;
const SHINE_BAR_W = TOK_LAYOUT_COL_LEFT_W - 2 * TOK_SPACE_SM;
const SHINE_BAR_H = TOK_METER_SM;
const shineBarX = [SHINE_BAR_X, SHINE_BAR_X];  // set by drawLimitCard (pace meter lefts)
const shineBarY = [-1, -1], shineFillPx = [-1, -1], shinePrevCenter = [null, null];
let shineBarW = SHINE_BAR_W;                   // set by drawLimitCard (the meters' width)
let sweepStartMs = 0, sweepActive = false;
function shineColor(i, center) {
  const d = Math.abs(i - center);
  return d <= 2 ? TOK_COLOR_DATA_PACE_SHINE_HI : d <= 6 ? TOK_COLOR_DATA_PACE_SHINE_MID : d <= SHINE_BAND_R ? TOK_COLOR_DATA_PACE_SHINE_LO : TOK_COLOR_DATA_PACE;
}
function drawShineStrip(barIdx, center, force) {
  const fillW = shineFillPx[barIdx], y = shineBarY[barIdx];
  if (fillW < 14 || y < 0) return false;
  const prev = force ? null : shinePrevCenter[barIdx];
  if (prev === center) return false;
  shinePrevCenter[barIdx] = center;
  let changed = false;
  for (let i = 2; i <= fillW - 3; i++) {
    const c = shineColor(i, center);
    if (prev !== null && c === shineColor(i, prev)) continue;
    gfx.fillRect(shineBarX[barIdx] + i, y, 1, SHINE_BAR_H, c);
    changed = true;
  }
  return changed;
}
function limitCardsPage() {
  const p = STATE.currentPage;
  return p === 0 || p === MIXED_PAGE || p === NOTE_PAGE || p === OVERVIEW_PAGE;
}
function sweepCenter(nowMs) {
  const t = Math.min(1, (nowMs - sweepStartMs) / (TOK_MOTION_SWEEP_MS * motionTimeScale));
  return -SHINE_BAND_R + Math.trunc(t * (shineBarW + 2 * SHINE_BAND_R));
}
function shineTick(nowMs) {
  // One sweep every TOK_MOTION_SWEEP_PERIOD_MS, measured start to start.
  if (!sweepActive && !STATE.cfgReduceMotion && STATE.cfgShowCountdown && limitCardsPage() &&
      (sweepStartMs === 0 || nowMs - sweepStartMs >= TOK_MOTION_SWEEP_PERIOD_MS * motionTimeScale)) {
    sweepActive = true; sweepStartMs = nowMs || 1;
    shinePrevCenter[0] = shinePrevCenter[1] = null;
  }
  if (!sweepActive) return false;
  if (!limitCardsPage() || !STATE.cfgShowCountdown) { sweepActive = false; return false; }
  const center = sweepCenter(nowMs);
  const done = center >= shineBarW + SHINE_BAND_R;
  let changed = false;
  for (let i = 0; i < 2; i++) if (drawShineStrip(i, center, false)) changed = true;
  if (done) sweepActive = false;
  return changed;
}

// ── SHARED LEFT COLUMN ────────────────────────────────────────
function drawPaceFlag(x, baseline, ahead, currentPct, elapsedSec, remainingSec, xMax) {
  if (!ahead) return;
  const x2 = drawText(TOK_TYPE_HEADLINE, x, baseline - fontAscent(TOK_TYPE_HEADLINE), '!', TOK_COLOR_STATUS_ERROR);
  const dur = formatPaceDur(currentPct, elapsedSec, remainingSec);
  if (dur && x2 + TOK_SPACE_HAIR + textW(TOK_TYPE_BODY, dur) <= xMax) drawText(TOK_TYPE_BODY, x2 + TOK_SPACE_HAIR, baseline - fontAscent(TOK_TYPE_BODY), dur, TOK_COLOR_TEXT_SECONDARY);
}
// Compact (the Overview's 89px cells) puts both captions on one row.
// countdown is '4h 03m' (no 'in'), '' when unknown.
function drawLimitCard(cardX, cardY, cardW, cardH, compact, label, percent, ahead, elapsed, rem, pace, barIdx, resets, countdown) {
  drawCard(cardX, cardY, cardW, cardH);
  const x = cardX + TOK_SPACE_SM, w = cardW - 2 * TOK_SPACE_SM;
  // numeral.lg ink starts 6px below its line top and the last caption's ends at
  // +94: 10px of air above and below the ink.
  const top = cardY + TOK_SPACE_XS;
  const baseline = top + fontAscent(TOK_TYPE_NUMERAL_LG);
  const xe = percent >= 0 ? drawText(TOK_TYPE_NUMERAL_LG, x, top, percent + '%',
                                     dataStale() ? TOK_COLOR_TEXT_SECONDARY : TOK_COLOR_DATA_USAGE)
                          : drawText(TOK_TYPE_NUMERAL_LG, x, top, '--', TOK_COLOR_TEXT_TERTIARY);
  const lx = xe + TOK_SPACE_SM;
  drawSectionLabel(lx, baseline - fontAscent(TOK_TYPE_LABEL), label);
  drawPaceFlag(lx + textW(TOK_TYPE_LABEL, label) + TOK_SPACE_XS, baseline, ahead, percent, elapsed, rem, x + w);
  let y = top + fontLineH(TOK_TYPE_NUMERAL_LG) + TOK_SPACE_STACK_TIGHT;
  drawMeter(x, y, w, TOK_METER_MD, percent, TOK_COLOR_DATA_USAGE);
  y += TOK_METER_MD + TOK_SPACE_STACK_TIGHT;
  if (STATE.cfgShowCountdown) {
    shineBarX[barIdx] = x;
    shineBarY[barIdx] = y;
    shineBarW = w;
    shineFillPx[barIdx] = drawMeter(x, y, w, SHINE_BAR_H, pace, TOK_COLOR_DATA_PACE);
    if (sweepActive) drawShineStrip(barIdx, sweepCenter(performance.now()), true);
    y += SHINE_BAR_H + TOK_SPACE_STACK_TIGHT;
  } else {
    shineBarY[barIdx] = -1; shineFillPx[barIdx] = -1;
  }
  // Compact: the countdown right-aligned on the reset time's row, shortened
  // on measured width (pages.cpp's twin).
  let inText = countdown ? 'in ' + countdown : '';
  let rightW = 0;
  if (compact && inText) {
    rightW = TOK_SPACE_SM + textW(TOK_TYPE_CAPTION, inText);
    if (textW(TOK_TYPE_CAPTION, resets || '') + rightW > w) {
      inText = countdown;
      rightW = TOK_SPACE_SM + textW(TOK_TYPE_CAPTION, inText);
    }
  }
  // "Resets Thu 04:59" is 113px, 1 over the 112 inner: it drops the word.
  const fitsWord = textW(TOK_TYPE_CAPTION, 'Resets ') + textW(TOK_TYPE_CAPTION, resets || '') + rightW <= w;
  const rx = fitsWord ? drawText(TOK_TYPE_CAPTION, x, y, 'Resets ', TOK_COLOR_TEXT_SECONDARY) : x;
  if (resets) drawText(TOK_TYPE_CAPTION, rx, y, resets, TOK_COLOR_TEXT_SECONDARY);
  else drawText(TOK_TYPE_CAPTION, rx, y, '--', TOK_COLOR_TEXT_TERTIARY);
  if (!inText) return;
  if (compact) drawTextR(TOK_TYPE_CAPTION, x + w, y, inText, TOK_COLOR_TEXT_SECONDARY);
  else drawText(TOK_TYPE_CAPTION, x, y + 17, inText, TOK_COLOR_TEXT_SECONDARY);
}
// Twin of pages.cpp's weekResetWeekdayLabel(): "Oct 1, 04:59" -> "Thu 04:59".
function weekResetWeekdayLabel(weekRem, resets) {
  if (!resets) return resets;
  const commaIdx = resets.indexOf(',');
  if (weekRem < 0 || commaIdx < 0) return resets;
  return bangkokWeekdayAt(weekRem) + ' ' + resets.slice(commaIdx + 1).trimStart();
}
// The 5H and WK cards: the left column, or the Overview's two top-left cells.
function drawLimitsColumn(overview = false) {
  const sessionRem = liveResetsInSec(STATE.sessionResetsInSec);
  const weekRem = liveResetsInSec(STATE.weekResetsInSec);
  const sessionPace = elapsedPercentOfWindow(sessionRem, SESSION_WINDOW_SEC);
  const weekPace = elapsedPercentOfWindow(weekRem, WEEK_WINDOW_SEC);
  const sessionAhead = STATE.sessionPercent >= 0 && sessionPace >= 0 && STATE.sessionPercent > sessionPace + 2;
  const weekAhead = STATE.weekPercent >= 0 && weekPace >= 0 && STATE.weekPercent > weekPace + 2;
  const sessionElapsed = sessionRem >= 0 ? SESSION_WINDOW_SEC - sessionRem : -1;
  const weekElapsed = weekRem >= 0 ? WEEK_WINDOW_SEC - weekRem : -1;
  const x0 = overview ? OV_COL_X[0] : TOK_LAYOUT_COL_LEFT_X, w0 = overview ? OV_COL_W[0] : TOK_LAYOUT_COL_LEFT_W;
  const x1 = overview ? OV_COL_X[1] : TOK_LAYOUT_COL_LEFT_X, w1 = overview ? OV_COL_W[1] : TOK_LAYOUT_COL_LEFT_W;
  const y0 = overview ? OV_ROW_Y[0] : LIMIT5H_Y, y1 = overview ? OV_ROW_Y[0] : LIMITWK_Y;
  const h = overview ? OV_ROW_H[0] : LIMIT_CARD_H;
  drawLimitCard(x0, y0, w0, h, overview, '5H', STATE.sessionPercent, sessionAhead, sessionElapsed, sessionRem, sessionPace, 0,
                STATE.sessionResets, sessionRem >= 0 ? fmtCountdown(sessionRem) : '');
  drawLimitCard(x1, y1, w1, h, overview, 'WK', STATE.weekPercent, weekAhead, weekElapsed, weekRem, weekPace, 1,
                weekResetWeekdayLabel(weekRem, STATE.weekResets), weekRem >= 0 ? fmtCountdownDHM(weekRem) : '');
}
// Bitcoin logo (twin of pages.cpp's drawBtcLogo).
const BTC_LOGO_R = 7;
function drawBtcLogo(cx, cy) {
  const B = ['.X.X.', 'XXXX.', 'X...X', 'X...X', 'XXXX.', 'X...X', 'X...X', 'XXXX.', '.X.X.'];
  gfx.fillSmoothCircle(cx, cy, BTC_LOGO_R, TOK_BTC_ORANGE);
  for (let r = 0; r < 9; r++)
    for (let c = 0; c < 5; c++)
      if (B[r][c] === 'X') gfx.fillRect(cx - 2 + c, cy - 4 + r, 1, 1, TOK_COLOR_BG_CANVAS);
}
// The rows keep the 60px card's places in a taller box (the Overview cell):
// the extra height goes to the bottom.
function drawBtcCard(cardX = TOK_LAYOUT_COL_LEFT_X, cardY = BTC_Y, cardW = TOK_LAYOUT_COL_LEFT_W, cardH = BTC_CARD_H) {
  drawCard(cardX, cardY, cardW, cardH);
  const x = cardX + TOK_SPACE_SM, w = cardW - 2 * TOK_SPACE_SM;
  const cy = cardY + TOK_SPACE_SM + BTC_LOGO_R;
  drawBtcLogo(x + BTC_LOGO_R, cy);
  if (STATE.btcPrice >= 0 && STATE.btcChangePct !== null)
    drawTextR(TOK_TYPE_NUMERAL_SM, x + w, cy - Math.trunc(fontLineH(TOK_TYPE_NUMERAL_SM) / 2),
              fmtChangePct(STATE.btcChangePct),
              STATE.btcChangePct < 0 ? TOK_COLOR_STATUS_ERROR : TOK_COLOR_STATUS_SUCCESS);
  // numeral.lg ink ends 27px below its line top.
  const baseline = cardY + BTC_CARD_H - TOK_SPACE_SM - 27 + fontAscent(TOK_TYPE_NUMERAL_LG);
  if (STATE.btcPrice < 0) { drawText(TOK_TYPE_NUMERAL_LG, x, baseline - fontAscent(TOK_TYPE_NUMERAL_LG), '--', TOK_COLOR_TEXT_TERTIARY); return; }
  const price = fmtBtc(STATE.btcPrice);
  let pf = TOK_TYPE_NUMERAL_LG;
  if (textW(pf, price) > w) pf = TOK_TYPE_NUMERAL_MD;
  if (textW(pf, price) > w) pf = TOK_TYPE_NUMERAL_SM;
  drawText(pf, x, baseline - fontAscent(pf), price, TOK_COLOR_TEXT_PRIMARY);
}

// ── CLOCK ─────────────────────────────────────────────────────
function drawTimerWedge(cx, cy, r, startAngle, endAngle) {
  let delta = (endAngle - startAngle) % 360;
  if (delta < 0) delta += 360;
  gfx.fillArc(cx, cy, r, startAngle, startAngle + delta, TOK_COLOR_DATA_PACE_WEDGE);
}
function drawAnalogClock(cx, cy, r, hour24, minute, second, haveReset, resetHour24, resetMinute) {
  const hourAngle = ((hour24 % 12) + minute / 60) * 30 - 90;
  const minAngle = (minute + second / 60) * 6 - 90;
  const secAngle = second * 6 - 90;
  const rad = (a) => a * Math.PI / 180;
  gfx.aaRing(cx, cy, r, 2, TOK_COLOR_TEXT_PRIMARY);
  if (haveReset && STATE.cfgShowCountdown)
    drawTimerWedge(cx, cy, r - 2, hourAngle, ((resetHour24 % 12) + resetMinute / 60) * 30 - 90);
  for (let i = 0; i < 12; i++) {
    const t = rad(i * 30 - 90);
    gfx.drawWideLine(cx + Math.trunc(Math.cos(t) * (r - 2)), cy + Math.trunc(Math.sin(t) * (r - 2)),
                     cx + Math.trunc(Math.cos(t) * (r - 8)), cy + Math.trunc(Math.sin(t) * (r - 8)), 1.0, TOK_COLOR_TEXT_PRIMARY);
  }
  const hx = cx + Math.trunc(Math.cos(rad(hourAngle)) * r * 0.5), hy = cy + Math.trunc(Math.sin(rad(hourAngle)) * r * 0.5);
  const mx = cx + Math.trunc(Math.cos(rad(minAngle)) * r * 0.8), my = cy + Math.trunc(Math.sin(rad(minAngle)) * r * 0.8);
  const sx = cx + Math.trunc(Math.cos(rad(secAngle)) * (r - 3)), sy = cy + Math.trunc(Math.sin(rad(secAngle)) * (r - 3));
  gfx.drawWideLine(cx, cy, hx, hy, 3.0, TOK_COLOR_TEXT_PRIMARY);
  gfx.drawWideLine(cx, cy, mx, my, 2.0, TOK_COLOR_TEXT_PRIMARY);
  if (haveReset) {
    const ra = rad(((resetHour24 % 12) + resetMinute / 60) * 30 - 90);
    gfx.drawWideLine(cx, cy, cx + Math.trunc(Math.cos(ra) * (r - 3)), cy + Math.trunc(Math.sin(ra) * (r - 3)), 0.8, TOK_COLOR_DATA_PACE);
  }
  gfx.drawWideLine(cx, cy, sx, sy, 1.2, TOK_COLOR_ACCENT);
  gfx.fillSmoothCircle(cx, cy, 3, TOK_COLOR_TEXT_PRIMARY);
}

function aqiColors(aqi) {
  if (aqi <= 50) return [TOK_AQI_GOOD, TOK_GRAY_0];
  if (aqi <= 100) return [TOK_AQI_MODERATE, TOK_GRAY_0];
  if (aqi <= 150) return [TOK_AQI_USG, TOK_GRAY_0];
  if (aqi <= 200) return [TOK_AQI_UNHEALTHY, TOK_GRAY_0];
  if (aqi <= 300) return [TOK_AQI_VERY_UNHEALTHY, TOK_GRAY_0];
  return [TOK_AQI_HAZARDOUS, TOK_GRAY_6];
}
function aqiBadgeW(aqi) { return textW(TOK_TYPE_HEADLINE, String(aqi)) + 2 * TOK_BADGE_PAD_X; }
function drawAqiBadge(x, y, aqi) {
  const [bg, fg] = aqiColors(aqi);
  gfx.fillSmoothRoundRect(x, y, aqiBadgeW(aqi), TOK_BADGE_H, TOK_RADIUS_SM, bg);
  drawText(TOK_TYPE_HEADLINE, x + TOK_BADGE_PAD_X, y + Math.trunc((TOK_BADGE_H - fontLineH(TOK_TYPE_HEADLINE)) / 2), String(aqi), fg);
}
