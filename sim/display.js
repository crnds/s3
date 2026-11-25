// sim/display.js -- frame buffers, RGB565 scrim/fade and the presents (src/display.cpp).

// ── SIMULATED GFX (the LovyanGFX calls the firmware makes) ──────
// `ctx` draws into frameCanvas, the firmware's off-screen `frame`; presents
// composite it onto the visible canvas (screenCtx), like display.cpp.
const canvas = document.getElementById('screen');
const screenCtx = canvas.getContext('2d');
function makeFrame() {
  const c = document.createElement('canvas');
  c.width = SCREEN_W; c.height = SCREEN_H;
  return c;
}
const frameCanvas = makeFrame();
let ctx = frameCanvas.getContext('2d', { willReadFrequently: true });  // swapped to the weather icon mask while it draws
const prevFrame = makeFrame(), behindFrame = makeFrame();
function copyFrame(dst, src) { dst.getContext('2d').drawImage(src, 0, 0); }
// ── RGB565 MATERIALS (display.cpp's scrimPx / fadePx) ──────────
// Pixels are quantised to RGB565, shifted/masked exactly as the firmware
// does, and expanded back -- so a scrim or a fade matches the board.
function to565(r, g, b) { return ((Math.round(r * 31 / 255) << 11) | (Math.round(g * 63 / 255) << 5) | Math.round(b * 31 / 255)); }
function to888(c, out, i) {
  out[i] = Math.round(((c >> 11) & 31) * 255 / 31);
  out[i + 1] = Math.round(((c >> 5) & 63) * 255 / 63);
  out[i + 2] = Math.round((c & 31) * 255 / 31);
}
function scrim565(c, level) {
  if (level === 1) return (c - ((c >> 2) & 0x39E7)) & 0xFFFF;
  return (c >> 1) & 0x7BEF;
}
function fade565(a, b, level) {
  if (level === 1) return (a - ((a >> 2) & 0x39E7) + ((b >> 2) & 0x39E7)) & 0xFFFF;
  if (level === 2) return (((a >> 1) & 0x7BEF) + ((b >> 1) & 0x7BEF)) & 0xFFFF;
  return (((a >> 2) & 0x39E7) + b - ((b >> 2) & 0x39E7)) & 0xFFFF;
}
function scrimImage(img, level) {
  const d = img.data;
  for (let i = 0; i < d.length; i += 4) to888(scrim565(to565(d[i], d[i + 1], d[i + 2]), level), d, i);
}

// ── PRESENT (display.cpp twin) ────────────────────────────────
let transitionDirty = false, presentHold = false;
function presentFrame() {
  if (presentHold) return;
  if (navTransitionActive()) { transitionDirty = true; return; }
  presentPlain();
}
function presentPlain() {
  screenCtx.fillStyle = TOK_COLOR_BG_CANVAS;
  screenCtx.fillRect(0, 0, SCREEN_W, SCREEN_H);
  screenCtx.drawImage(frameCanvas, STATE.shiftX, STATE.shiftY);
  STATE.shiftDirty = false;
}
function presentSlide(from, to, off, forward, pinY0 = SCREEN_H) {
  off = Math.max(0, Math.min(SCREEN_W, off));
  const sx = STATE.shiftX, sy = STATE.shiftY;
  screenCtx.fillStyle = TOK_COLOR_BG_CANVAS;
  screenCtx.fillRect(0, 0, SCREEN_W, SCREEN_H);
  screenCtx.save();
  screenCtx.beginPath();
  screenCtx.rect(0, 0, SCREEN_W, pinY0);
  screenCtx.clip();
  if (forward) { screenCtx.drawImage(from, sx - off, sy); screenCtx.drawImage(to, sx + SCREEN_W - off, sy); }
  else { screenCtx.drawImage(from, sx + off, sy); screenCtx.drawImage(to, sx + off - SCREEN_W, sy); }
  screenCtx.restore();
  // Pinned band (design.md 7's status strip): always `to`, unmoved, so it
  // doesn't slide with the page above it.
  if (pinY0 < SCREEN_H) {
    screenCtx.drawImage(to, 0, pinY0, SCREEN_W, SCREEN_H - pinY0, sx, sy + pinY0, SCREEN_W, SCREEN_H - pinY0);
  }
}
function presentSheet(behind, sheet, vis, level) {
  vis = Math.max(0, vis);
  const sx = STATE.shiftX, sy = STATE.shiftY;
  const top = SCREEN_H - vis;
  screenCtx.fillStyle = TOK_COLOR_BG_CANVAS;
  screenCtx.fillRect(0, 0, SCREEN_W, SCREEN_H);
  if (top > 0) {
    const bctx = behind.getContext('2d', { willReadFrequently: true });
    const img = bctx.getImageData(0, 0, SCREEN_W, top);
    if (level > 0) scrimImage(img, level);
    screenCtx.putImageData(img, sx, sy);
  }
  screenCtx.drawImage(sheet, 0, 0, SCREEN_W, Math.min(SCREEN_H, vis), sx, sy + top, SCREEN_W, Math.min(SCREEN_H, vis));
}
function presentFade(from, to, level) {
  const a = from.getContext('2d', { willReadFrequently: true }).getImageData(0, 0, SCREEN_W, SCREEN_H);
  const b = to.getContext('2d', { willReadFrequently: true }).getImageData(0, 0, SCREEN_W, SCREEN_H);
  const d = a.data, e = b.data;
  for (let i = 0; i < d.length; i += 4)
    to888(fade565(to565(d[i], d[i + 1], d[i + 2]), to565(e[i], e[i + 1], e[i + 2]), level), d, i);
  screenCtx.fillStyle = TOK_COLOR_BG_CANVAS;
  screenCtx.fillRect(0, 0, SCREEN_W, SCREEN_H);
  screenCtx.putImageData(a, STATE.shiftX, STATE.shiftY);
}
