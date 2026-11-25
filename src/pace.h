#pragma once
// Quota pacing and the sound-alert trigger: pure logic, no Arduino dependency
// (native unit tests, roadmap O4). Pace = % of a limit's window elapsed; the
// pace flag and the "runs out before the reset" alert both come from it.
#include <stddef.h>
#include <stdint.h>

const long SESSION_WINDOW_SEC = 5L * 3600;       // 5h
const long WEEK_WINDOW_SEC = 7L * 24 * 3600;     // 168h

// Pace flag deadband: usage must exceed the window's elapsed % by more than
// this to be "ahead", which stops flicker at the boundary.
const int PACE_DEADBAND_PCT = 2;
// The run-out alert waits until this much of the window has passed.
const int ALERT_MIN_ELAPSED_PCT = 10;

// % of the window elapsed (0 right after a reset, 100 right before the next);
// -1 when remainingSec is unknown (< 0).
int elapsedPercentOfWindow(long remainingSec, long windowSec);

// Projected seconds until 100% "if this pace continues" (the average rate since
// the window's start), or -1 when unknown or the reset would land first.
long paceSecondsToLimit(int currentPct, long elapsedSec, long remainingSec);

// The pace flag's projection text: "45m" / "3h", or "" (see above).
void formatPaceDurBuf(char* out, size_t n, int currentPct, long elapsedSec, long remainingSec);

// True when usage is known, pace is known, and usage > pace + deadband.
bool paceAhead(int usedPct, int pacePct);

// Per-limit state for the sound alerts (one per window).
struct LimitAlert {
  int lastPct = -1;
  bool runOut = false;  // already alerted "runs out before the reset" this window
  bool full = false;    // already alerted "reached 100%" this window
};
// True when this poll newly crosses one of the limit's alert conditions.
// `prime` records the state without reporting (first poll after boot/outage).
bool limitAlertCrossed(LimitAlert& a, int pct, long remSec, long windowSec, bool prime);
