// sim/timing.js -- stale / offline thresholds, live countdown, Bangkok clock (src/timing.cpp).

// Stale data (pages.cpp twin): the last good poll is older than max(2 polls,
// 1 min), or there has been none since load and this is the cached copy.
const STALE_AFTER_MIN_MS = 60000;
function dataStale() {
  if (!STATE.haveData) return false;
  if (STATE.lastFetchSuccessMs === 0) return true;
  return performance.now() - STATE.lastFetchSuccessMs > Math.max(applyEffectivePoll() * 2, STALE_AFTER_MIN_MS);
}

function liveResetsInSec(baseSec) {
  if (baseSec < 0) return -1;
  const rem = baseSec - Math.floor((performance.now() - STATE.lastFetchOkMs) / 1000);
  return rem < 0 ? 0 : rem;
}

// Bangkok time (no DST, fixed UTC+7 -- the firmware's GMT_OFFSET_SEC).
// Weekday name (Intl 'short', matches WDAY_ABBR) at now + offsetSec, in
// Bangkok time -- used to relabel the week-reset date with a weekday name.
function bangkokWeekdayAt(offsetSec) {
  const d = new Date(simNowMs() + offsetSec * 1000);
  return new Intl.DateTimeFormat('en-US', { timeZone: 'Asia/Bangkok', weekday: 'short' }).format(d);
}

function bangkokParts() {
  const fmt = new Intl.DateTimeFormat('en-US', {
    timeZone: 'Asia/Bangkok', hour12: false,
    weekday: 'short', month: 'short', day: 'numeric', year: 'numeric',
    hour: '2-digit', minute: '2-digit', second: '2-digit',
  });
  const parts = {};
  for (const p of fmt.formatToParts(new Date(simNowMs()))) parts[p.type] = p.value;
  const out = {
    weekday: parts.weekday, month: parts.month, day: parts.day, year: parts.year,
    hour: parts.hour === '24' ? '00' : parts.hour, minute: parts.minute, second: parts.second,
  };
  return out;
}

// Offline after max(OFFLINE_AFTER_MS, 3 polls) of wall time without a
// successful poll -- main.cpp's offlineAfterMs(), not a cycle count.
function offlineAfterMs() { return Math.max(OFFLINE_AFTER_MS, applyEffectivePoll() * 3); }
