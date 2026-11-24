// Spring integrator, velocity tracker, momentum projection and rubber-banding
// (motion.h): pure maths with no Arduino dependency, so `pio test -e native`
// can pin it (roadmap O4). The backlight animator is in motion.cpp. Twin of
// simulator-s3.html's MOTION block -- same integrator, same clamps, so a
// transition takes the same frames in both.
#include <math.h>

#include "motion.h"
#include "tokens.h"

float motionTimeScale = 1.0f;

// Arduino's PI, a double literal: keeps springTo's arithmetic identical.
static const double kPi = 3.1415926535897932384626433832795;

static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

void springTo(Spring& s, float target, uint16_t zeta100, uint16_t responseMs) {
  s.target = target;
  s.zeta = zeta100 / 100.0f;
  s.w = 2.0f * kPi / (responseMs / 1000.0f * motionTimeScale);
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
  vx = clampf(vx, -4000.0f, 4000.0f);
  vy = clampf(vy, -4000.0f, 4000.0f);
}

float projectDistance(float v) {
  return (v / 1000.0f) * TOK_TOUCH_DECEL / (1.0f - TOK_TOUCH_DECEL);
}

float rubberband(float over, float dim) {
  return over * dim * TOK_TOUCH_RUBBERBAND / (dim + TOK_TOUCH_RUBBERBAND * fabsf(over));
}
