// Flash (NVS) config: loadRuntimeConfig, the early theme read, the cached server IP and the
// generic save helpers. Split from sd_store.cpp (roadmap O10).
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

// Just the theme, before the first frame: painting the boot canvas in the
// wrong theme would flash it until loadRuntimeConfig() catches up.
void loadThemeEarly() {
  Preferences p;
  p.begin(CFG_NS, true);
  cfgLightMode = p.isKey(CONFIG_KEY_NAMES[CFGKEY_LIGHT_MODE]) &&
                 p.getInt(CONFIG_KEY_NAMES[CFGKEY_LIGHT_MODE]) != 0;
  p.end();
  applyThemeTokens();
}

// The Projects and Limits pages merged into one Usage page (page 1), so every
// later page moved down one slot. Saved boot_page / last_page values from
// before that (0 Status, 1 Projects, 2 Limits, 3 Cats ... 6 Note) are shifted
// once: 2 and up lose one, so Limits lands on Usage and Cats on 2. The marker
// key stops it running again; -1 (Auto) and 0..1 are already right.
static void migratePageIndices() {
  Preferences p;
  p.begin(CFG_NS, false);
  if (!p.isKey("pages_v2")) {
    const ConfigKeyId keys[2] = {CFGKEY_BOOT_PAGE, CFGKEY_LAST_PAGE};
    for (ConfigKeyId k : keys) {
      const char* name = CONFIG_KEY_NAMES[k];
      if (p.isKey(name) && p.getInt(name) >= 2) p.putInt(name, p.getInt(name) - 1);
    }
    p.putUChar("pages_v2", 1);
  }
  p.end();
}

// Optional override for the compiled config.h defaults. A key simply absent
// from flash (first boot, or never changed since) means "use the compiled
// default" -- not an error worth surfacing.
void loadRuntimeConfig() {
  migratePageIndices();
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
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_LIGHT_MODE])) {
    cfgLightMode = p.getInt(CONFIG_KEY_NAMES[CFGKEY_LIGHT_MODE]) != 0;
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_ALERT_VOL])) {
    cfgAlertVol = constrain(p.getInt(CONFIG_KEY_NAMES[CFGKEY_ALERT_VOL]), SOUND_VOL_OFF, SOUND_VOL_MAX);
  }
  if (p.isKey(CONFIG_KEY_NAMES[CFGKEY_TAP_VOL])) {
    cfgTapVol = constrain(p.getInt(CONFIG_KEY_NAMES[CFGKEY_TAP_VOL]), SOUND_VOL_OFF, SOUND_VOL_MAX);
  }
  const struct { int key; bool* var; } switches[] = {
    {CFGKEY_HOURLY_CHIME, &cfgHourlyChime}, {CFGKEY_CLAUDE_DING, &cfgClaudeDing},
    {CFGKEY_SOUND_ALERTS, &cfgSoundAlerts}, {CFGKEY_MUTE_SLEEP, &cfgMuteSleep},
    {CFGKEY_MUTE_NIGHT, &cfgMuteNight}, {CFGKEY_OTA_AUTO, &cfgOtaAuto},
  };
  for (const auto& sw : switches)
    if (p.isKey(CONFIG_KEY_NAMES[sw.key])) *sw.var = p.getInt(CONFIG_KEY_NAMES[sw.key]) != 0;
  p.end();
  applyThemeTokens();  // includes applyContrast()

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
