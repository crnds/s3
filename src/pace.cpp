// Pace maths and alert crossing (pace.h). Moved out of pages.cpp / main.cpp.
#include "pace.h"

#include <stdio.h>

int elapsedPercentOfWindow(long remainingSec, long windowSec) {
  if (remainingSec < 0) return -1;
  long elapsed = windowSec - remainingSec;
  if (elapsed < 0) elapsed = 0;
  int pct = (int)(elapsed * 100 / windowSec);
  return pct < 0 ? 0 : (pct > 100 ? 100 : pct);
}

long paceSecondsToLimit(int currentPct, long elapsedSec, long remainingSec) {
  if (currentPct <= 0 || elapsedSec <= 0 || remainingSec < 0) return -1;
  float remainingPct = 100 - currentPct;
  if (remainingPct <= 0) return -1;
  float ratePerSec = (float)currentPct / (float)elapsedSec;
  float secToExhaust = remainingPct / ratePerSec;
  if (secToExhaust >= remainingSec) return -1;  // reset wins, say nothing
  return (long)secToExhaust;
}

void formatPaceDurBuf(char* out, size_t n, int currentPct, long elapsedSec, long remainingSec) {
  long secToExhaust = paceSecondsToLimit(currentPct, elapsedSec, remainingSec);
  if (secToExhaust < 0) { if (n) out[0] = 0; return; }
  if (secToExhaust < 3600) {
    int m = (int)(secToExhaust / 60.0f + 0.5f);
    snprintf(out, n, "%dm", m < 1 ? 1 : m);
    return;
  }
  snprintf(out, n, "%dh", (int)(secToExhaust / 3600.0f + 0.5f));
}

bool paceAhead(int usedPct, int pacePct) {
  return usedPct >= 0 && pacePct >= 0 && usedPct > pacePct + PACE_DEADBAND_PCT;
}

bool limitAlertCrossed(LimitAlert& a, int pct, long remSec, long windowSec, bool prime) {
  if (pct < 0 || remSec < 0) return false;  // limit unavailable: keep the last state
  if (a.lastPct >= 0 && pct < a.lastPct) a.runOut = a.full = false;  // the window reset
  a.lastPct = pct;
  long elapsed = windowSec - remSec;
  bool runOut = elapsed * 100 >= windowSec * ALERT_MIN_ELAPSED_PCT &&
                paceSecondsToLimit(pct, elapsed, remSec) >= 0;
  bool full = pct >= 100;
  bool crossed = (runOut && !a.runOut) || (full && !a.full);
  a.runOut = a.runOut || runOut;
  a.full = a.full || full;
  return crossed && !prime;
}
