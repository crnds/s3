// Spring integrator, velocity tracker and backlight animator (motion.h).
// Twin of simulator-s3.html's MOTION block -- same integrator, same clamps,
// so a transition takes the same frames in both.
#include "state.h"

float motionTimeScale = 1.0f;

void springTo(Spring& s, float target, uint16_t zeta100, uint16_t responseMs) {
  s.target = target;
  s.zeta = zeta100 / 100.0f;
  s.w = 2.0f * PI / (responseMs / 1000.0f * motionTimeScale);
  s.active = true;
}

bool springStep(Spring& s, float dtSec) {
  if (!s.active) return false;
  if (dtSec > 0.05f) dtSec = 0.05f;
  float h = dtSec / 4.0f;
  for (int i = 0; i < 4; i++) {
    float a = -s.w * s.w * (s.x - s.target) - 2.0f * s.zeta * s.w * s.v;
    s.v += a * h;
    s.x += s.v * h;
  }
  if (fabsf(s.x - s.target) < 0.5f && fabsf(s.v) < 15.0f) {
    s.x = s.target;
    s.v = 0;
    s.active = false;
  }
  return true;
}

void velocityReset(VelocityTracker& vt) { vt.n = 0; vt.head = 0; }

void velocityAdd(VelocityTracker& vt, int32_t x, int32_t y, uint32_t t) {
  vt.x[vt.head] = x;
  vt.y[vt.head] = y;
  vt.t[vt.head] = t;
  vt.head = (vt.head + 1) % 4;
  if (vt.n < 4) vt.n++;
}

void velocityGet(const VelocityTracker& vt, uint32_t now, float& vx, float& vy) {
  vx = vy = 0;
  if (vt.n < 2) return;
  int newest = (vt.head + 3) % 4;
  if (now - vt.t[newest] > 50) return;  // the finger paused before lifting
  int oldest = newest;
  for (int k = 1; k < vt.n; k++) {
    int i = (newest - k + 4) % 4;
    if (vt.t[newest] - vt.t[i] > TOK_TOUCH_VELOCITY_WINDOW_MS) break;
    oldest = i;
  }
  uint32_t dt = vt.t[newest] - vt.t[oldest];
  if (dt == 0) return;
  vx = (vt.x[newest] - vt.x[oldest]) * 1000.0f / dt;
  vy = (vt.y[newest] - vt.y[oldest]) * 1000.0f / dt;
  vx = constrain(vx, -4000.0f, 4000.0f);
  vy = constrain(vy, -4000.0f, 4000.0f);
}

float projectDistance(float v) {
  return (v / 1000.0f) * TOK_TOUCH_DECEL / (1.0f - TOK_TOUCH_DECEL);
}

float rubberband(float over, float dim) {
  return over * dim * TOK_TOUCH_RUBBERBAND / (dim + TOK_TOUCH_RUBBERBAND * fabsf(over));
}

// ── BACKLIGHT ────────────────────────────────────────────────
static float blLevel = 0;            // what the PWM shows now
static float blFrom = 0, blTarget = 0;
static uint32_t blStartMs = 0, blDurMs = 0;
static int breathLeft = 0;           // half-dips still to play
static uint32_t breathStartMs = 0;

static void blApply() { displaySetBrightness((uint8_t)(blLevel + 0.5f)); }

void backlightFadeTo(uint8_t target, uint32_t fadeMs) {
  blFrom = blLevel;
  blTarget = target;
  blStartMs = millis();
  blDurMs = (uint32_t)(fadeMs * motionTimeScale);
  if (blDurMs == 0) {
    blLevel = target;
    blApply();
  }
}

void backlightBreath() {
  breathLeft = TOK_MOTION_BACKLIGHT_BREATH_COUNT;
  breathStartMs = millis();
}

uint8_t backlightLevel() { return (uint8_t)(blLevel + 0.5f); }

void backlightTick(uint32_t now) {
  float base = blTarget;
  if (blDurMs > 0) {
    uint32_t el = now - blStartMs;
    if (el >= blDurMs) {
      blDurMs = 0;
    } else {
      float t = (float)el / blDurMs;
      float e = t * t * (3.0f - 2.0f * t);  // smoothstep: no brightness jump at either end
      base = blFrom + (blTarget - blFrom) * e;
    }
  }
  float level = base;
  if (breathLeft > 0) {
    uint32_t dur = (uint32_t)(TOK_MOTION_BACKLIGHT_BREATH_MS * motionTimeScale);
    uint32_t el = now - breathStartMs;
    if (el >= dur) {
      breathLeft--;
      breathStartMs = now;
      el = 0;
    }
    if (breathLeft > 0) {
      // One breath = down to 40% and back up, a raised-cosine dip.
      float t = (float)el / dur;
      float dip = 0.5f - 0.5f * cosf(2.0f * PI * t);
      level = base * (1.0f - dip * (1.0f - TOK_MOTION_BACKLIGHT_BREATH_PCT / 100.0f));
    }
  }
  if (fabsf(level - blLevel) >= 0.5f || (blDurMs == 0 && breathLeft == 0 && blLevel != blTarget)) {
    blLevel = (blDurMs == 0 && breathLeft == 0) ? blTarget : level;
    blApply();
  }
}
