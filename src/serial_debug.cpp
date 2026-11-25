// Serial debug keys and the F/f fixture protocol (tools/parity.py, tools/grab_screen.py).
// Split from main.cpp (roadmap O10).
#include "state.h"
#include "pace.h"
#include "serial_debug.h"

static long realEpochAtFixture = 0;     // the true time when the first 'F' arrived, to put back on 'f'
static uint32_t realMsAtFixture = 0;

// ── SERIAL DEBUG HOOKS ─────────────────────────────────────
// Single-key commands over the USB serial port, so pages can be driven and
// screenshotted without touching the board (tools/grab_screen.py):
//   n / p   next / previous page        w / d / s   Weather / Device / Settings
//   x       close any sheet             g           grab: raw frame dump
//   z       toggle screen sleep         m           slow motion (x10) on/off
static void closeSheetNow() {
  if (navSheetOpen()) navCloseSheet();
  navFinishTransition();
}

uint32_t hourlyTestStartMs = 0;  // serial 'h': play the hourly signal now

// One line from the serial port (CR/LF stripped), or what arrived before the
// timeout. Debug-only and blocking: used by the 'F' fixture command.
static String serialReadLine(uint32_t timeoutMs) {
  String s;
  uint32_t t0 = millis();
  while (millis() - t0 < timeoutMs) {
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n') return s;
      if (c != '\r') s += c;
      t0 = millis();  // the sender is still talking
    }
    delay(1);
  }
  return s;
}

void serialCommand(char c) {
  switch (c) {
    case 'F': {
      // "F<epoch> <light 0|1>\n", wait for "[fixture] send", then "<usage json on
      // one line>\n": the canned data and
      // frozen clock for tools/parity.py (roadmap O7). Applies the document as a
      // cache load would, sets the wall clock, the theme (RAM only, never saved)
      // and page 0, then freezes polling until 'f'. Same inputs as the
      // simulator's ?fixture=<name>.
      String head = serialReadLine(1000);
      long epoch = head.toInt();
      int sp = head.indexOf(' ');
      bool light = sp >= 0 && head.substring(sp + 1).toInt() != 0;
      // loop() can be mid-render (~100 ms) when 'F' arrives, and the USB receive
      // buffer holds only 256 bytes: the sender waits for this before the JSON.
      if (epoch > 0) Serial.println("[fixture] send");
      String json = epoch > 0 ? serialReadLine(4000) : String();
      if (epoch <= 0 || !applyUsageJson(json, false)) {
        Serial.println("[fixture] FAILED");
        break;
      }
      struct tm clockCheck;
      if (!fixtureMode && haveLocalTime(&clockCheck)) {  // remember the true time (first load only)
        realEpochAtFixture = (long)time(nullptr);
        realMsAtFixture = millis();
      }
      applyTimezone();  // idempotent; keeps the fixture's 17:09 (+07:00) from showing as 10:09
      struct timeval tv = { (time_t)epoch, 0 };
      settimeofday(&tv, nullptr);
      fixtureEpoch = epoch;
      fixtureMode = true;
      if (cfgLightMode != light) {
        cfgLightMode = light;
        applyThemeTokens();
        navInvalidatePosters();
      }
      closeSheetNow();
      navGoToPage(0, true);
      Serial.println("[fixture] on");
      break;
    }
    case 'f':
      fixtureMode = false;
      if (realEpochAtFixture > 0) {  // the true time, advanced by what passed meanwhile
        struct timeval back = { (time_t)(realEpochAtFixture + (millis() - realMsAtFixture) / 1000), 0 };
        settimeofday(&back, nullptr);
        realEpochAtFixture = 0;
      }
      loadThemeEarly();  // the fixture's theme was RAM-only: back to the saved one
      navInvalidatePosters();
      Serial.println("[fixture] off (polling resumes, saved theme and true clock restored)");
      break;
    case 'h':
      hourlyTestStartMs = millis() | 1;  // never 0, which means "not running"
      audioPlay(SND_HOURLY);
      Serial.println("[hourly] test signal");
      break;
    case 'k': {  // play every sound in turn (still subject to Off / sleep / night gating)
      static uint8_t nextSound = 0;
      Serial.printf("[audio] sound %u\n", nextSound);
      audioPlay((SoundId)nextSound);
      nextSound = (nextSound + 1) % SND_COUNT;
      break;
    }
    case 'n': case 'p':
      closeSheetNow();
      navGoToPage(c == 'n' ? (currentPage + 1) % PAGE_COUNT : (currentPage - 1 + PAGE_COUNT) % PAGE_COUNT, c == 'n');
      break;
    case 'w': closeSheetNow(); navOpenSheet(0); break;
    case 'd': closeSheetNow(); navOpenSheet(1); break;
    case 's': closeSheetNow(); navOpenSheet(2); break;
    case 'x': navCloseSheet(); break;
    case 'z':
      if (screenSleeping) exitScreenSleep();
      else enterScreenSleep();
      break;
    case 'm':
      motionTimeScale = motionTimeScale > 1.0f ? 1.0f : 10.0f;
      Serial.printf("[motion] time scale x%.0f\n", motionTimeScale);
      break;
    case 'g': {
      // Header line, raw 480x320 big-endian RGB565 (the sprite's own bytes),
      // then a trailer line. grab_screen.py syncs on the header.
      Serial.printf("\nS3SHOT %d %d\n", SCREEN_W, SCREEN_H);
      Serial.flush();
      const uint8_t* p = (const uint8_t*)frame.getBuffer();
      const size_t total = SCREEN_W * SCREEN_H * 2;
      for (size_t off = 0; off < total; off += 4096) {
        size_t n = min((size_t)4096, total - off);
        Serial.write(p + off, n);
      }
      Serial.flush();
      Serial.print("\nS3END\n");
      break;
    }
  }
}

