// Backlight animator (motion.h). The spring integrator and velocity tracker
// are in motion_math.cpp (native-tested); both are twins of simulator-s3.html's
// MOTION block -- same integrator, same clamps, so a transition takes the same
// frames in both.
#include "state.h"

// ── BACKLIGHT ────────────────────────────────────────────────
static float blLevel = 0;            // what the PWM shows now
static float blFrom = 0, blTarget = 0;
static uint32_t blStartMs = 0, blDurMs = 0;

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
  if (fabsf(level - blLevel) >= 0.5f || (blDurMs == 0 && blLevel != blTarget)) {
    blLevel = (blDurMs == 0) ? blTarget : level;
    blApply();
  }
}
