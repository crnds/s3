// SD-card diagnostics log + self-hosted BMP splash loader, and flash (NVS)
// config load/save. Carried over from the CYD firmware.
#include "state.h"

// ── RUNTIME CONFIG (NVS FLASH) ──────────────────────────────
// Settings/config (WiFi creds, server host/port, and every Settings-UI
// value) live in internal flash via the Preferences
// library (NVS) so they work with no SD card present at all. Namespace
// "s3cfg" (its own namespace -- this is a different board from the CYD, and
// the CYD's SD-era /config.json migration and touch-calibration keys are
// gone: capacitive touch needs no calibration); a Preferences instance is opened/closed per call rather than
// held open -- no mutex needed, since only networkTask (core 0) writes it
// after boot and this file's one boot-time load never overlaps that.
static const char* CFG_NS = "s3cfg";

// Optional override for the compiled config.h defaults. A key simply absent
// from flash (first boot, or never changed since) means "use the compiled
// default" -- not an error worth surfacing.
void loadRuntimeConfig() {
  Preferences p;
  p.begin(CFG_NS, true);  // read-only

  // wifi_ssid/wifi_password from flash are used only when config.h's
  // compiled WIFI_SSID is empty -- exactly the "never configured, let the AP
  // setup portal's saved result take over" case (see runApSetup()). A real
  // WIFI_SSID baked into config.h always wins, so editing config.h and
  // reflashing -- the normal way to change WiFi -- isn't silently shadowed
  // forever by whatever was once submitted through the portal.
  if (cfgWifiSsid.length() == 0) {
    if (p.isKey("wifi_ssid")) cfgWifiSsid = p.getString("wifi_ssid");
    if (p.isKey("wifi_password")) cfgWifiPassword = p.getString("wifi_password");
  }
  if (p.isKey("server_host")) cfgServerHost = p.getString("server_host");
  if (p.isKey("server_port")) cfgServerPort = p.getInt("server_port");
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_BRIGHTNESS])) {
    cfgBrightness = constrain(p.getInt(CONFIG_KEY_NAMES[CFGKEY_BRIGHTNESS]), 0, 255);
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_POLL_INTERVAL])) {
    // Clamp to a sane floor so a typo can't hammer the Mac.
    // Stored as the user preference; applyEffectivePoll() (below) turns it
    // into the live POLL_INTERVAL_MS, which Battery Save may stretch.
    int sec = p.getInt(CONFIG_KEY_NAMES[CFGKEY_POLL_INTERVAL]);
    cfgPollIntervalSec = (uint32_t)constrain(sec, 5, 3600);
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_ROTATION])) {
    cfgScreenRotation = p.getInt(CONFIG_KEY_NAMES[CFGKEY_ROTATION]);
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_PIXEL_SHIFT])) {
    // Minutes per anti-retention orbit step; 0 pins the frame at (0,0).
    int m = constrain(p.getInt(CONFIG_KEY_NAMES[CFGKEY_PIXEL_SHIFT]), 0, 60);
    cfgShiftStepMs = (uint32_t)m * 60000;
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_BOOT_PAGE])) {
    int v = p.getInt(CONFIG_KEY_NAMES[CFGKEY_BOOT_PAGE]);
    cfgBootPage = (v == BOOT_PAGE_AUTO) ? BOOT_PAGE_AUTO : constrain(v, 0, PAGE_COUNT - 1);
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_LAST_PAGE])) {
    cfgLastPage = constrain(p.getInt(CONFIG_KEY_NAMES[CFGKEY_LAST_PAGE]), 0, PAGE_COUNT - 1);
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_CAT_SHUFFLE])) {
    // 0 disables the early cutoff (GIFs always play to their natural end);
    // -1 is FIXED (disables auto-rotation entirely -- see catShuffleFixed).
    // The -1 branch is explicit rather than folded into the *1000 cast below,
    // since (uint32_t)(-1) would otherwise wrap to a huge millisecond value.
    int s = constrain(p.getInt(CONFIG_KEY_NAMES[CFGKEY_CAT_SHUFFLE]), -1, 300);
    catShuffleFixed = (s < 0);
    catShuffleMs = catShuffleFixed ? 0 : (uint32_t)s * 1000;
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_NIGHT_MODE])) {
    cfgNightModeOn = p.getInt(CONFIG_KEY_NAMES[CFGKEY_NIGHT_MODE]) != 0;
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_SHOW_COUNTDOWN])) {
    cfgShowCountdown = p.getInt(CONFIG_KEY_NAMES[CFGKEY_SHOW_COUNTDOWN]) != 0;
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_SHOW_AQI])) {
    cfgShowAqi = p.getInt(CONFIG_KEY_NAMES[CFGKEY_SHOW_AQI]) != 0;
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_HOURLY_FLASH])) {
    cfgHourlyFlash = p.getInt(CONFIG_KEY_NAMES[CFGKEY_HOURLY_FLASH]) != 0;
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_SHOW_PROGRESS])) {
    cfgShowProgress = p.getInt(CONFIG_KEY_NAMES[CFGKEY_SHOW_PROGRESS]) != 0;
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_BATTERY_SAVE])) {
    // 0=OFF, 1=ON, 2=AUTO (legacy 0/1 still map correctly).
    cfgBatterySaveMode = constrain(p.getInt(CONFIG_KEY_NAMES[CFGKEY_BATTERY_SAVE]),
                                   BATTERY_SAVE_OFF, BATTERY_SAVE_AUTO);
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_REDUCE_MOTION])) {
    cfgReduceMotion = p.getInt(CONFIG_KEY_NAMES[CFGKEY_REDUCE_MOTION]) != 0;
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_HIGH_CONTRAST])) {
    cfgHighContrast = p.getInt(CONFIG_KEY_NAMES[CFGKEY_HIGH_CONTRAST]) != 0;
  }
  p.end();
  applyContrast();

  // Apply after all related keys are loaded so Battery Save can floor the
  // poll interval against the user's poll_interval_sec preference. AUTO
  // won't floor until a live fetch sets serverBatterySave.
  applyEffectivePoll();
  Serial.println("[config] loaded overrides from flash");
}

// Last mDNS-resolved server address, cached in NVS as a raw 4-byte IPv4 under
// the key "server_ip". Not a user setting (it never appears in the Settings
// area) — purely a boot-time shortcut so a board that reboots while multicast
// is unhealthy can still reach the Mac without waiting for a working mDNS
// lookup. Written via the generic saveIntConfigToFlash(). 0 = nothing cached.
uint32_t loadServerIpFromFlash() {
  Preferences p;
  p.begin(CFG_NS, true);  // read-only
  uint32_t ip = p.isKey(SERVER_IP_KEY) ? (uint32_t)p.getInt(SERVER_IP_KEY, 0) : 0;
  p.end();
  return ip;
}

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
  if (getLocalTime(&timeinfo, 0)) {
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
bool drawBmpFromSD(const char* path, int dx, int dy) {
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

// ── FLASH CONFIG WRITES ─────────────────────────────────────
// NVS is a wear-levelled, transactional key-value store: a single put() here
// can't corrupt or lose any other key, so (unlike the old SD-JSON approach)
// there's no read-modify-write-the-whole-file dance needed -- each of these
// just opens the namespace, writes its own key(s), and closes.

// Called from ap_setup.cpp's runApSetup(), which runs before networkTask/
// loop() start, so no cross-core handoff is needed here.
void saveWifiCredsToFlash(const String& ssid, const String& password) {
  Preferences p;
  p.begin(CFG_NS, false);
  p.putString("wifi_ssid", ssid);
  p.putString("wifi_password", password);
  p.end();
}

// Persists any single-int Settings value to flash. Called from networkTask
// (core 0) only -- see pendingConfigSave in main.cpp's
// networkTask(). One generic function serves every setting whose live value
// is a plain int, rather than a near-duplicate save function per setting.
void saveIntConfigToFlash(const char* key, int32_t value) {
  Preferences p;
  p.begin(CFG_NS, false);
  p.putInt(key, value);
  p.end();
}

// Erases the saved WiFi credentials from flash (Forget WiFi), leaving every
// other key untouched. Only meaningful when config.h's compiled WIFI_SSID is
// blank -- if it's baked in, cfgWifiSsid will still be non-empty at the next
// boot regardless of this, and runApSetup() won't trigger (same precedent as
// loadRuntimeConfig(): a compiled WIFI_SSID always wins). Called from
// networkTask (core 0) only, via pendingForgetWifi.
void forgetWifiFromFlash() {
  Preferences p;
  p.begin(CFG_NS, false);
  p.remove("wifi_ssid");
  p.remove("wifi_password");
  p.end();
}
