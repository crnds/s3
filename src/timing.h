#pragma once
// The two wall-clock rules that decide how old data looks: STALE (the strip
// says "Updated Nm ago", numerals go secondary) and OFFLINE (the dashboard
// gives way to the cat screen). Pure functions of (now, poll interval, last
// good poll) so they can be tested natively (roadmap O4); pages.cpp and
// main.cpp call them with millis() and the live globals. design.md 13.4.
#include <stdint.h>

// Stale after max(2 poll intervals, this): one missed poll is not news.
const uint32_t STALE_AFTER_MIN_MS = 60000;
// Offline after max(this, 3 poll intervals) without a good poll. A congested
// WLAN drops connects in bursts (66-268 s gaps measured), and Battery Save
// stretches the poll to 120 s, so a fixed 60 s flipped to the cat screen
// between healthy polls.
const uint32_t OFFLINE_AFTER_MS = 180000UL;

// lastOkMs == 0 means no good live poll since boot (the SD cache is showing).
bool dataStaleAt(bool haveData, uint32_t lastOkMs, uint32_t pollMs, uint32_t nowMs);
uint32_t offlineAfterMsFor(uint32_t pollMs);
// Tick a server-provided "seconds remaining" down by the wall time since the
// poll that carried it; -1 stays -1, and it never goes below 0.
long liveResetsFrom(long baseSec, uint32_t nowMs, uint32_t fetchOkMs);
