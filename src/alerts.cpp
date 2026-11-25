// Sound alerts read off each poll (design.md 12.10).
// Split from main.cpp (roadmap O10).
#include "state.h"
#include "pace.h"

// ── SOUND ALERTS (Settings > Sound, design.md 12.10) ───────
// Events read off the poll, each played once per crossing; audioPlay() applies
// the category toggles, the alert volume and the mute rules. networkTask only.
//  - Alerts (SND_ALERT): a 5h or weekly limit projected to run out before its
//    reset (once 10% of the window has passed -- before that the average rate
//    is minutes of data), a limit reaching 100%, and the board going offline.
//    Usage % only falls at a reset, so a fall re-arms that window's alerts.
//  - Claude ding (SND_DONE): the server's active_now going true -> false --
//    Claude Code has written nothing for 180 s (server/usage_server.py's ACTIVE_WINDOW_SEC), so
//    it finished or is waiting on you.
// The first live poll after boot or an outage only primes the state, so a
// condition that was already true makes no sound.
// LimitAlert, ALERT_MIN_ELAPSED_PCT and limitAlertCrossed() live in pace.h.
static LimitAlert sessionAlert, weekAlert;
static bool alertsPrimed = false;
static bool claudeWasActive = false;

void pollAlerts(bool liveOk, bool wentOffline) {
  if (wentOffline) {
    if (alertsPrimed) audioPlay(SND_ALERT);
    alertsPrimed = false;  // re-prime on the first good poll back
    return;
  }
  if (!liveOk) return;
  lockState();
  int sessionPct = STATE.sessionPercent, weekPct = STATE.weekPercent;
  long sessionRem = STATE.sessionResetsInSec, weekRem = STATE.weekResetsInSec;
  int8_t active = STATE.claudeActive;
  unlockState();

  bool prime = !alertsPrimed;
  bool alert = limitAlertCrossed(sessionAlert, sessionPct, sessionRem, SESSION_WINDOW_SEC, prime);
  if (limitAlertCrossed(weekAlert, weekPct, weekRem, WEEK_WINDOW_SEC, prime)) alert = true;
  if (alert) audioPlay(SND_ALERT);
  if (active >= 0) {
    if (!prime && claudeWasActive && !active) audioPlay(SND_DONE);
    claudeWasActive = active;
  }
  alertsPrimed = true;
}

