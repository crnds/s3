// Staleness / offline thresholds and the live countdown (timing.h).
#include "timing.h"

bool dataStaleAt(bool haveData, uint32_t lastOkMs, uint32_t pollMs, uint32_t nowMs) {
  if (!haveData) return false;
  if (lastOkMs == 0) return true;
  uint32_t after = pollMs * 2;
  if (after < STALE_AFTER_MIN_MS) after = STALE_AFTER_MIN_MS;
  return nowMs - lastOkMs > after;
}

uint32_t offlineAfterMsFor(uint32_t pollMs) {
  uint32_t byPoll = pollMs * 3;
  return byPoll > OFFLINE_AFTER_MS ? byPoll : OFFLINE_AFTER_MS;
}

long liveResetsFrom(long baseSec, uint32_t nowMs, uint32_t fetchOkMs) {
  if (baseSec < 0) return -1;
  long rem = baseSec - (long)((nowMs - fetchOkMs) / 1000);
  return rem < 0 ? 0 : rem;
}
