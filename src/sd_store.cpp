// SD-card diagnostics log, reset-reason text and the self-hosted BMP splash loader.
// The flash (NVS) config moved to config_store.cpp.
#include "state.h"

const char* resetReasonStr() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    default: return "UNKNOWN";
  }
}

// Appends one timestamped event to /diag_log.csv — meant to be read later
// off the card, not surfaced on screen. Never blocks retrying: a failed
// write is dropped, not queued.
void logDiag(const char* event) {
  if (!STATE.sdOk) return;

  struct tm timeinfo;
  char tsBuf[24];
  if (haveLocalTime(&timeinfo)) {
    snprintf(tsBuf, sizeof(tsBuf), "%04d-%02d-%02d %02d:%02d:%02d",
              timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
              timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
  } else {
    snprintf(tsBuf, sizeof(tsBuf), "boot+%lus", (unsigned long)(millis() / 1000));
  }

  lockSD();
  File f = SD_MMC.open("/diag_log.csv", FILE_APPEND);
  if (f) {
    f.printf("%s,%s\n", tsBuf, event);
    f.close();
  }
  unlockSD();
}

// ── SELF-HOSTED ASSETS ─────────────────────────────────────
// Minimal 24-bit uncompressed BMP loader so image assets can live on the SD
// card instead of being baked into flash. BMP rows are bottom-up unless height
// is negative; each row is read into a small static buffer, converted to
// RGB565, and drawn into `frame` (the caller presents). Returns false
// (silently) for a missing file or any unsupported format -- callers treat the
// asset as simply absent.
static bool drawBmpFromSD(const char* path, int dx, int dy) {
  if (!STATE.sdOk) return false;
  lockSD();
  File f = SD_MMC.open(path, FILE_READ);
  if (!f) { unlockSD(); return false; }

  uint8_t hdr[54];
  if (f.read(hdr, 54) != 54 || hdr[0] != 'B' || hdr[1] != 'M') { f.close(); unlockSD(); return false; }
  uint32_t dataOffset = hdr[10] | (hdr[11] << 8) | (hdr[12] << 16) | ((uint32_t)hdr[13] << 24);
  int32_t  width  = hdr[18] | (hdr[19] << 8) | (hdr[20] << 16) | ((uint32_t)hdr[21] << 24);
  int32_t  height = hdr[22] | (hdr[23] << 8) | (hdr[24] << 16) | ((uint32_t)hdr[25] << 24);
  uint16_t bpp    = hdr[28] | (hdr[29] << 8);
  if (bpp != 24 || width <= 0 || width > SCREEN_W || height == 0 || abs(height) > SCREEN_H) {
    f.close();
    unlockSD();
    return false;
  }

  bool topDown = height < 0;
  int32_t h = topDown ? -height : height;
  int rowSize = (width * 3 + 3) & ~3;  // BMP rows are padded to a 4-byte boundary

  static uint8_t rowBuf[SCREEN_W * 3];
  static uint16_t pxBuf[SCREEN_W];
  for (int32_t r = 0; r < h; r++) {
    int32_t srcRow = topDown ? r : (h - 1 - r);
    f.seek(dataOffset + (uint32_t)srcRow * rowSize);
    if (f.read(rowBuf, rowSize) != rowSize) break;
    for (int32_t x = 0; x < width; x++) {
      uint8_t b  = rowBuf[x * 3];
      uint8_t g8 = rowBuf[x * 3 + 1];
      uint8_t rr = rowBuf[x * 3 + 2];
      pxBuf[x] = ((rr & 0xF8) << 8) | ((g8 & 0xFC) << 3) | (b >> 3);
    }
    g->pushImage(dx, dy + r, width, 1, pxBuf);
  }
  f.close();
  unlockSD();
  return true;
}

// Optional boot splash: if the user dropped a 480x320 24-bit /splash.bmp on the
// card, show it briefly before the WiFi spinner. Absent file -> no splash, no
// delay, straight into the normal boot.
void showBootSplash() {
  g->fillScreen(TOK_COLOR_BG_CANVAS);
  if (drawBmpFromSD("/splash.bmp", 0, 0)) {
    presentFrame();
    delay(1500);
  }
}
