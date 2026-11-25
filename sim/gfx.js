// sim/gfx.js -- the LovyanGFX calls the firmware makes (fills, AA shapes, clip).

function rgbaOf(color, a) {
  const m = /rgb\((\d+),(\d+),(\d+)\)/.exec(color);
  return `rgba(${m[1]},${m[2]},${m[3]},${a})`;
}

const gfx = {
  fillScreen(c) { ctx.fillStyle = c; ctx.fillRect(0, 0, SCREEN_W, SCREEN_H); },
  fillRect(x, y, w, h, c) { ctx.fillStyle = c; ctx.fillRect(x, y, w, h); },
  drawRect(x, y, w, h, c) {
    ctx.strokeStyle = c; ctx.lineWidth = 1;
    ctx.strokeRect(x + 0.5, y + 0.5, w - 1, h - 1);
  },
  drawRoundRect(x, y, w, h, r, c) {
    ctx.strokeStyle = c; ctx.lineWidth = 1;
    ctx.beginPath(); ctx.roundRect(x + 0.5, y + 0.5, w - 1, h - 1, r); ctx.stroke();
  },
  // Port of LovyanGFX 1.2.x fillSmoothRoundRect (LGFXBase.inl): solid spans
  // plus 1px alpha-blended edge pixels -- same algorithm, same thresholds,
  // so the AA corners land on the same pixels as the board's.
  fillSmoothRoundRect(x, y, w, h, r, c) {
    ctx.fillStyle = c;
    if (r > (w >> 1)) r = w >> 1;
    if (r > (h >> 1)) r = h >> 1;
    y += r; h -= 2 * r;
    if (h > 0 && w > 0) ctx.fillRect(x, y, w, h);
    h--;
    x += r; w -= 2 * r + 1;
    const r1 = r * r;
    r++;
    const r2 = r * r;
    const LO = 1 / 32, HI = 1 - LO;
    let xs = 0, cx = 0;
    for (let cy = r - 1; cy > 0; cy--) {
      const dy2 = (r - cy) * (r - cy);
      for (cx = xs; cx < r; cx++) {
        const hyp2 = (r - cx) * (r - cx) + dy2;
        if (hyp2 <= r1) break;
        if (hyp2 >= r2) continue;
        const alphaf = r - Math.sqrt(hyp2);
        if (alphaf > HI) break;
        xs = cx;
        if (alphaf < LO) continue;
        ctx.fillStyle = rgbaOf(c, Math.floor(alphaf * 255) / 255);
        ctx.fillRect(x + cx - r, y + cy - r, 1, 1);
        ctx.fillRect(x - cx + r + w, y + cy - r, 1, 1);
        ctx.fillRect(x - cx + r + w, y - cy + r + h, 1, 1);
        ctx.fillRect(x + cx - r, y - cy + r + h, 1, 1);
        ctx.fillStyle = c;
      }
      const len = 2 * (r - cx) + 1 + w;
      if (len > 0) {
        ctx.fillStyle = c;
        ctx.fillRect(x + cx - r, y + cy - r, len, 1);
        ctx.fillRect(x + cx - r, y - cy + r + h, len, 1);
      }
    }
  },
  fillSmoothCircle(x, y, r, c) { this.fillSmoothRoundRect(x - r, y - r, r * 2 + 1, r * 2 + 1, r, c); },
  // pages.cpp's aaRing: only the band's pixels, coverage = outer minus inner disc.
  aaRing(cx, cy, r, t, c) {
    const ro = r + 0.5, ri = r - t + 0.5;
    const clamp = (v) => Math.max(0, Math.min(1, v));
    for (let dy = -r - 1; dy <= r + 1; dy++) {
      const xo2 = (ro + 1) * (ro + 1) - dy * dy;
      if (xo2 < 0) continue;
      const xo = Math.ceil(Math.sqrt(xo2));
      const xi2 = (ri - 1) * (ri - 1) - dy * dy;
      const xi = xi2 > 0 ? Math.floor(Math.sqrt(xi2)) : 0;
      for (let dx = xi; dx <= xo; dx++) {
        const d = Math.sqrt(dx * dx + dy * dy);
        const cov = clamp(ro - d) - clamp(ri - d);
        if (cov < 1 / 32) continue;
        ctx.fillStyle = cov >= 1 ? c : rgbaOf(c, Math.floor(cov * 255) / 255);
        ctx.fillRect(cx + dx, cy + dy, 1, 1);
        if (dx) ctx.fillRect(cx - dx, cy + dy, 1, 1);
      }
    }
  },
  // pages.cpp's aaRingF: float edges ro > ri (ri <= 0 = disc), coverage x alpha.
  aaRingF(cx, cy, ro, ri, c, alpha) {
    const rMax = Math.ceil(ro);
    const clamp = (v) => Math.max(0, Math.min(1, v));
    for (let dy = -rMax; dy <= rMax; dy++) {
      for (let dx = 0; dx <= rMax; dx++) {
        const d = Math.sqrt(dx * dx + dy * dy);
        const cov = (clamp(ro - d) - clamp(ri - d)) * alpha / 255;
        if (cov < 1 / 32) continue;
        ctx.fillStyle = cov >= 1 ? c : rgbaOf(c, Math.floor(cov * 255) / 255);
        ctx.fillRect(cx + dx, cy + dy, 1, 1);
        if (dx) ctx.fillRect(cx - dx, cy + dy, 1, 1);
      }
    }
  },
  fillTriangle(x0, y0, x1, y1, x2, y2, c) {
    ctx.fillStyle = c; ctx.beginPath();
    ctx.moveTo(x0 + 0.5, y0 + 0.5); ctx.lineTo(x1 + 0.5, y1 + 0.5); ctx.lineTo(x2 + 0.5, y2 + 0.5);
    ctx.closePath(); ctx.fill();
  },
  // LovyanGFX drawWideLine(x0,y0,x1,y1,r,color): r is the half-width.
  drawWideLine(x0, y0, x1, y1, r, c) {
    ctx.strokeStyle = c; ctx.lineWidth = r * 2; ctx.lineCap = 'round';
    ctx.beginPath(); ctx.moveTo(x0 + 0.5, y0 + 0.5); ctx.lineTo(x1 + 0.5, y1 + 0.5); ctx.stroke();
    ctx.lineCap = 'butt';
  },
  // LovyanGFX fillArc(x,y,r0,r1,angle0,angle1,color) with r1=0: a pie wedge, degrees.
  fillArc(cx, cy, r, a0, a1, c) {
    ctx.fillStyle = c; ctx.beginPath(); ctx.moveTo(cx + 0.5, cy + 0.5);
    ctx.arc(cx + 0.5, cy + 0.5, r, a0 * Math.PI / 180, a1 * Math.PI / 180, false);
    ctx.closePath(); ctx.fill();
  },
  setClipRect(x, y, w, h) {
    if (this._clipped) ctx.restore();
    ctx.save(); ctx.beginPath(); ctx.rect(x, y, w, h); ctx.clip();
    this._clipped = true;
  },
  clearClipRect() { if (this._clipped) { ctx.restore(); this._clipped = false; } },
};
