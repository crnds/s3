// Backlight animator (motion.h). The spring integrator and velocity tracker
// are in motion_math.cpp (native-tested); both are twins of simulator-s3.html's
// MOTION block -- same integrator, same clamps, so a transition takes the same
// frames in both.
#include "state.h"

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
