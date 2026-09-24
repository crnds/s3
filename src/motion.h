#pragma once
// Motion primitives (design.md section 12): the spring integrator every
// spatial transition uses, the release-velocity tracker, momentum projection,
// rubber-banding, and the backlight animator (the one channel where smooth
// change costs no frames). simulator-s3.html mirrors each function.
#include <stdint.h>

// A critically-damped-or-softer spring, stepped once per loop() pass.
// x = presentation offset (px), v = px/s. Retargeting changes only `target`,
// so position and velocity carry on -- interruptible by construction.
struct Spring {
  float x = 0, v = 0, target = 0;
  float zeta = 1.0f, w = 25.13f;   // w = 2*PI / response
  bool active = false;
};
// Start (or retarget) toward `target` with the given token parameters
// (zeta x100, response ms), keeping the current x and v.
void springTo(Spring& s, float target, uint16_t zeta100, uint16_t responseMs);
// Advance by real elapsed time (clamped to 50 ms, 4 semi-implicit Euler
// sub-steps). Returns true while still moving; on settle it snaps x to target
// and clears `active` (the caller presents once more).
bool springStep(Spring& s, float dtSec);

// Release velocity from the last touch samples (design.md 9.4): a ring of 4
// samples; (newest - oldest within 100 ms) / dt; 0 if the finger paused
// > 50 ms before lifting; clamped to +/-4000 px/s.
struct VelocityTracker {
  int32_t x[4], y[4];
  uint32_t t[4];
  int n = 0, head = 0;
};
void velocityReset(VelocityTracker& vt);
void velocityAdd(VelocityTracker& vt, int32_t x, int32_t y, uint32_t t);
void velocityGet(const VelocityTracker& vt, uint32_t now, float& vx, float& vy);

// Where a flick would come to rest: (v / 1000) * d / (1 - d), d = touch.decel.
float projectDistance(float v);
// Resistance past a bound: over * dim * 0.55 / (dim + 0.55 * |over|).
float rubberband(float over, float dim);

// ── BACKLIGHT ANIMATOR ───────────────────────────────────────
// Fades the PWM level toward a target over a motion.backlight.* duration, and
// plays the hourly breath (dip to 40% and back, three times). Stepped from
// loop() every pass, including while nothing else presents.
void backlightFadeTo(uint8_t target, uint32_t fadeMs);
void backlightBreath();
void backlightTick(uint32_t now);
uint8_t backlightLevel();
