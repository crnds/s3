// Formatting (format.cpp).
#pragma once
#include <Arduino.h>

// ── FORMATTING (format.cpp) ────────────────────────────────
// Non-blocking "is the wall clock set?" + local broken-down time. Use this, never
// getLocalTime(&ti, 0): with ms == 0 the core's loop is `while (millis() - start
// <= 0)`, so a 1 ms tick between its two millis() reads makes it return false
// without ever reading the clock (~0.2% of calls; blanked the clock/calendar cards).
bool haveLocalTime(struct tm* ti);
String fmtTokens(int64_t t);
String fmtBtc(double p);
String fmtChangePct(double pct);  // "+5%" / "-3%" (whole percent, sign always)
String fmtCountdown(long sec);     // "4h 03m" / "42m"
String fmtCountdownDHM(long sec);  // "6d 16h" / "4h 03m"
String fmtKB(uint32_t bytes);
String fmtGB(uint64_t bytes);
int flashPercent(uint32_t &usedOut, uint32_t &totalOut);
// Internal-SRAM heap in use as a % of its total (heap_caps MALLOC_CAP_INTERNAL),
// and the same for the 8MB PSRAM -- the S3's two real memory pools, replacing
// the CYD's single fixed DRAM constant.
int staticRamPercent(uint32_t &usedOut, uint32_t &totalOut);
int psramPercent(uint32_t &usedOut, uint32_t &totalOut);
// SD_MMC.totalBytes()/usedBytes() touch the card that sdMutex serializes
// between networkTask (core 0) and the render core's cat-GIF reads (core 1)
// -- see sdMutex's contract above. refreshSdCapacityCache() does the live
// query under lockSD()/unlockSD() and must only be called from networkTask;
// drawDevicePage() (core 1, called from inside render()'s stateMutex-held
// section, so it cannot also take sdMutex -- only sdMutex -> stateMutex
// nesting is allowed, never the reverse) reads the cached result instead via
// cachedSdCapacityPercent(), no lock needed.
void refreshSdCapacityCache();
int cachedSdCapacityPercent(uint64_t &usedOut, uint64_t &totalOut);
