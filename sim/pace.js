// sim/pace.js -- pace math: window elapsed %, run-out projection (src/pace.cpp).

const SESSION_WINDOW_SEC = 5 * 3600, WEEK_WINDOW_SEC = 7 * 24 * 3600;
function elapsedPercentOfWindow(remainingSec, windowSec) {
  if (remainingSec < 0) return -1;
  const elapsed = Math.max(0, windowSec - remainingSec);
  return Math.max(0, Math.min(100, Math.floor(elapsed * 100 / windowSec)));
}
function formatPaceDur(currentPct, elapsedSec, remainingSec) {
  if (currentPct <= 0 || elapsedSec <= 0 || remainingSec < 0) return '';
  const remainingPct = 100 - currentPct;
  if (remainingPct <= 0) return '';
  const secToExhaust = remainingPct / (currentPct / elapsedSec);
  if (secToExhaust >= remainingSec) return '';
  if (secToExhaust < 3600) return Math.max(1, Math.floor(secToExhaust / 60 + 0.5)) + 'm';
  return Math.floor(secToExhaust / 3600 + 0.5) + 'h';
}
