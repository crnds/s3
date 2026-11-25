// Screen sleep: enterScreenSleep / exitScreenSleep.
// Split from main.cpp (roadmap O10).
#include "state.h"
#include "pace.h"
#include "screen_sleep.h"

// ── SCREEN SLEEP ───────────────────────────────────────────
// Tap the sleep button (corner slot a) -> the backlight fades to 0
// (motion.backlight.sleep), then panel DISPOFF + SLPIN, CPU down to 80MHz
// (lowest WiFi-safe clock; APB stays 80MHz so LEDC, SPI and I2C are
// unaffected), WiFi modem sleep, polls floored to the Battery Save cadence.
// loop() then only watches touch; the next press anywhere wakes and is
// swallowed. Nothing is presented while asleep, so the frame (and an open
// GIF's canvas) is exactly as it was on wake.
uint32_t sleepStartMs = 0;
static uint32_t cpuMhzBeforeSleep = 240;
bool swallowUntilUp = false;  // the waking press never reaches the screen

void enterScreenSleep() {
  if (screenSleeping) return;
  displaySetInvert(false);  // never sleep (or wake) with the panel mid-flash
  // The fade is short and nothing on screen needs input during it.
  backlightFadeTo(0, TOK_MOTION_BACKLIGHT_SLEEP_MS);
  uint32_t t0 = millis();
  while (millis() - t0 < (uint32_t)(TOK_MOTION_BACKLIGHT_SLEEP_MS * motionTimeScale) + 20) {
    backlightTick(millis());
    delay(10);
  }
  screenSleeping = true;
  sleepStartMs = millis();
  navResetGesture();
  applyEffectiveBrightness();
  displaySetSleep(true);
  cpuMhzBeforeSleep = getCpuFrequencyMhz();
  setCpuFrequencyMhz(80);
  WiFi.setSleep(true);
  applyEffectivePoll();
  Serial.printf("[sleep] enter (cpu %lu MHz, poll %lus)\n", (unsigned long)getCpuFrequencyMhz(),
                (unsigned long)(POLL_INTERVAL_MS / 1000));
}

void exitScreenSleep() {
  if (!screenSleeping) return;
  setCpuFrequencyMhz(cpuMhzBeforeSleep);
  WiFi.setSleep(false);
  screenSleeping = false;
  applyEffectivePoll();
  displaySetSleep(false);
  bool catMode = navCatLayout();
  if (settingsScreen != SET_OFF) renderSettings();
  else if (catMode && !weatherPageOpen && !devicePageOpen) presentFrame();
  else render();
  // DISPON first, then the backlight fades up (motion.backlight.wake): the
  // frame is intact, so the screen is effectively there at once.
  applyEffectiveBrightness(TOK_MOTION_BACKLIGHT_WAKE_MS);
  Serial.printf("[sleep] exit after %lus\n", (unsigned long)((millis() - sleepStartMs) / 1000));
}

