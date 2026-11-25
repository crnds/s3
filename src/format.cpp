// Formatting helpers + live resource-percent readers. The pure number/time
// formatting lives in format_pure.cpp (native-tested); these wrap it in String.
#include "state.h"

String fmtTokens(int64_t t) {
  char b[FMT_BUF_MIN];
  fmtTokensBuf(b, sizeof(b), t);
  return String(b);
}

String fmtBtc(double p) {
  char b[FMT_BUF_MIN];
  fmtBtcBuf(b, sizeof(b), p);
  return String(b);
}

String fmtChangePct(double pct) {
  char b[FMT_BUF_MIN];
  fmtChangePctBuf(b, sizeof(b), pct);
  return String(b);
}

bool haveLocalTime(struct tm* ti) {
  time_t now = time(nullptr);
  localtime_r(&now, ti);
  return ti->tm_year > (2016 - 1900);  // same "clock is set" test as getLocalTime()
}

String fmtCountdown(long sec) {
  char b[FMT_BUF_MIN];
  fmtCountdownBuf(b, sizeof(b), sec);
  return String(b);
}

// Day-aware countdown for windows that can span multiple days (the weekly
// reset): "6d 16h" / "4h 03m". Twin of simulator-s3.html's fmtCountdownDHM.
String fmtCountdownDHM(long sec) {
  char b[FMT_BUF_MIN];
  fmtCountdownDHMBuf(b, sizeof(b), sec);
  return String(b);
}

String fmtKB(uint32_t bytes) {
  return String((float)bytes / 1024.0f, 1) + "KB";
}

String fmtGB(uint64_t bytes) {
  return String((float)bytes / (1024.0f * 1024.0f * 1024.0f), 1) + "GB";
}

// Flash used by the sketch as a % of the OTA app partition. ESP.getSketchSize()
// walks the flash image and is measurably slow on this core, and the sketch
// size cannot change at runtime — computed once at boot (see main.cpp's
// setup()) and cached, rather than re-queried on every 1s footer/device-page
// render while stateMutex is held.
uint32_t cachedFlashUsed = 0, cachedFlashTotal = 0;
bool flashCacheValid = false;

int flashPercent(uint32_t &usedOut, uint32_t &totalOut) {
  if (!flashCacheValid) {
    cachedFlashUsed = ESP.getSketchSize();
    cachedFlashTotal = cachedFlashUsed + ESP.getFreeSketchSpace();
    flashCacheValid = true;
  }
  usedOut = cachedFlashUsed;
  totalOut = cachedFlashTotal;
  return cachedFlashTotal ? (int)((float)cachedFlashUsed / cachedFlashTotal * 100 + 0.5f) : -1;
}

// Internal SRAM in use (statics + heap + stacks' allocations) as a % of the
// internal pool. The S3 has two real pools -- ~512KB internal SRAM and 8MB
// PSRAM -- so the CYD's single fixed-DRAM-constant figure became these two.
int staticRamPercent(uint32_t &usedOut, uint32_t &totalOut) {
  uint32_t total = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
  uint32_t freeB = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  usedOut = total > freeB ? total - freeB : 0;
  totalOut = total;
  return total ? (int)((float)usedOut / total * 100 + 0.5f) : 0;
}

int psramPercent(uint32_t &usedOut, uint32_t &totalOut) {
  uint32_t total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  uint32_t freeB = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  usedOut = total > freeB ? total - freeB : 0;
  totalOut = total;
  return total ? (int)((float)usedOut / total * 100 + 0.5f) : -1;
}

// SD card space in use -- cheap FAT bookkeeping reads, not a scan of the
// card, but SD_MMC.totalBytes()/usedBytes() still touch the card, so this raw
// query must only run under lockSD()/unlockSD() from networkTask (core 0),
// never from the render core. -1 (with usedOut/totalOut untouched) when the
// card never mounted.
static int sdCapacityPercentRaw(uint64_t &usedOut, uint64_t &totalOut) {
  if (!STATE.sdOk) return -1;
  uint64_t total = SD_MMC.totalBytes();
  uint64_t used = SD_MMC.usedBytes();
  usedOut = used;
  totalOut = total;
  return total ? (int)((float)used / total * 100 + 0.5f) : -1;
}

// cachedSd* are plain (non-atomic) globals: refreshSdCapacityCache() (core 0,
// under sdMutex) is the only writer, drawDevicePage() (core 1) the only
// reader, and a torn read of a multi-byte value would at worst show one
// stale/mixed frame of a cosmetic stat -- self-correcting next refresh, and
// far cheaper than a lock render() can't take (see state.h's note on why
// stateMutex -> sdMutex nesting is forbidden).
static int cachedSdPercent = -1;
static uint64_t cachedSdUsedBytes = 0;
static uint64_t cachedSdTotalBytes = 0;

void refreshSdCapacityCache() {
  uint64_t used = 0, total = 0;
  lockSD();
  int pct = sdCapacityPercentRaw(used, total);
  unlockSD();
  cachedSdPercent = pct;
  cachedSdUsedBytes = used;
  cachedSdTotalBytes = total;
}

int cachedSdCapacityPercent(uint64_t &usedOut, uint64_t &totalOut) {
  usedOut = cachedSdUsedBytes;
  totalOut = cachedSdTotalBytes;
  return cachedSdPercent;
}
