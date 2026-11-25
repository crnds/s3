// sim/fonts_draw.js -- text from the firmware glyph atlas in sim/fonts.js (src/fonts.cpp).

// Text: the firmware's VLW glyphs, pre-tinted per colour into small canvases
// and composited with source-over -- the same alpha blend LovyanGFX does
// against the sprite (minus RGB565 quantization). Positioned by the two
// fonts.h rules: y is the line TOP (glyph top = y + maxAscent - dY), and a
// string is as wide as the sum of its advances.
const FONT = {};
for (const [id, f] of Object.entries(window.S3_FONTS || {})) {
  const glyphs = {};
  for (const [code, w, h, adv, dy, dx, b64] of f.glyphs) {
    const bin = atob(b64);
    const a = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) a[i] = bin.charCodeAt(i);
    glyphs[code] = { w, h, adv, dy, dx, alpha: a, tinted: {} };
  }
  FONT[id] = { maxAscent: f.maxAscent, lineHeight: f.lineHeight, glyphs };
}
function glyphOf(font, ch) {
  const f = FONT[font];
  let code = ch.charCodeAt(0);
  if (code < 0x20 || code > 0x7E) code = 0x3F;  // '?', same fold as fonts.cpp
  return f.glyphs[code];
}
function tintedGlyph(g, color) {
  let c = g.tinted[color];
  if (c) return c;
  c = document.createElement('canvas');
  c.width = Math.max(g.w, 1);
  c.height = Math.max(g.h, 1);
  const cx = c.getContext('2d');
  const img = cx.createImageData(c.width, c.height);
  const m = /rgb\((\d+),(\d+),(\d+)\)/.exec(color) || [0, 255, 255, 255];
  for (let i = 0; i < g.w * g.h; i++) {
    img.data[i * 4] = +m[1]; img.data[i * 4 + 1] = +m[2]; img.data[i * 4 + 2] = +m[3];
    img.data[i * 4 + 3] = g.alpha[i];
  }
  cx.putImageData(img, 0, 0);
  g.tinted[color] = c;
  return c;
}
function textW(font, s) {
  s = String(s);
  let w = 0;
  for (const ch of s) w += glyphOf(font, ch).adv;
  return w;
}
function fontLineH(font) { return FONT[font].lineHeight; }
function fontAscent(font) { return FONT[font].maxAscent; }
function drawText(font, x, y, s, color) {
  s = String(s);
  const f = FONT[font];
  let pen = x;
  for (const ch of s) {
    const g = glyphOf(font, ch);
    if (g.w > 0 && g.h > 0) ctx.drawImage(tintedGlyph(g, color), pen + g.dx, y + f.maxAscent - g.dy);
    pen += g.adv;
  }
  return x + textW(font, s);
}
function drawTextR(font, xRight, y, s, color) { return drawText(font, xRight - textW(font, s), y, s, color); }
function drawTextC(font, cx, y, s, color) { return drawText(font, cx - Math.trunc(textW(font, s) / 2), y, s, color); }
function drawChar(font, x, y, c, color) { drawText(font, x, y, c, color); }
