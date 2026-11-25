// sim/motion.js -- springs, velocity, rubber band and the backlight animator (src/motion.cpp).

// ── MOTION (motion.cpp twin) ──────────────────────────────────
function makeSpring() { return { x: 0, v: 0, target: 0, zeta: 1, w: 25.13, active: false }; }
function springTo(s, target, [zeta100, responseMs]) {
  s.target = target;
  s.zeta = zeta100 / 100;
  s.w = 2 * Math.PI / (responseMs / 1000 * motionTimeScale);
  s.active = true;
}
function springStep(s, dt) {
  if (!s.active) return false;
  if (dt > 0.05) dt = 0.05;
  const h = dt / 4;
  for (let i = 0; i < 4; i++) {
    const a = -s.w * s.w * (s.x - s.target) - 2 * s.zeta * s.w * s.v;
    s.v += a * h;
    s.x += s.v * h;
  }
  if (Math.abs(s.x - s.target) < 0.5 && Math.abs(s.v) < 15) { s.x = s.target; s.v = 0; s.active = false; }
  return true;
}
function makeVelocity() { return { s: [] }; }
function velocityAdd(vt, x, y, t) { vt.s.push({ x, y, t }); if (vt.s.length > 4) vt.s.shift(); }
function velocityGet(vt, now) {
  const s = vt.s;
  if (s.length < 2) return [0, 0];
  const newest = s[s.length - 1];
  if (now - newest.t > 50) return [0, 0];
  let oldest = newest;
  for (let k = s.length - 2; k >= 0; k--) {
    if (newest.t - s[k].t > TOK_TOUCH_VELOCITY_WINDOW_MS) break;
    oldest = s[k];
  }
  const dt = newest.t - oldest.t;
  if (dt <= 0) return [0, 0];
  const clamp = (v) => Math.max(-4000, Math.min(4000, v));
  return [clamp((newest.x - oldest.x) * 1000 / dt), clamp((newest.y - oldest.y) * 1000 / dt)];
}
function projectDistance(v) { return (v / 1000) * TOK_TOUCH_DECEL / (1 - TOK_TOUCH_DECEL); }
function rubberband(over, dim) { return over * dim * TOK_TOUCH_RUBBERBAND / (dim + TOK_TOUCH_RUBBERBAND * Math.abs(over)); }

// Backlight animator: the canvas brightness filter stands in for the PWM.
const BL = { level: 0, from: 0, target: 0, start: 0, dur: 0 };
let panelInverted = false;  // INVON/INVOFF twin: same filter chain as the backlight
function blApply() { canvas.style.filter = 'brightness(' + (BL.level / 255 * 100).toFixed(1) + '%)' + (panelInverted ? ' invert(1)' : ''); }
function displaySetInvert(on) { if (on === panelInverted) return; panelInverted = on; blApply(); }
function backlightFadeTo(target, fadeMs) {
  BL.from = BL.level; BL.target = target; BL.start = performance.now(); BL.dur = fadeMs * motionTimeScale;
  if (BL.dur === 0) { BL.level = target; blApply(); }
}
function backlightTick(now) {
  let base = BL.target;
  if (BL.dur > 0) {
    const el = now - BL.start;
    if (el >= BL.dur) BL.dur = 0;
    else { const t = el / BL.dur; base = BL.from + (BL.target - BL.from) * t * t * (3 - 2 * t); }
  }
  let level = base;
  if (BL.dur === 0) level = BL.target;
  if (Math.abs(level - BL.level) >= 0.5 || level !== BL.level) { BL.level = level; blApply(); }
}
