// SD store and flash config (sd_store.cpp).
#pragma once
#include <Arduino.h>

// ── SD STORE (sd_store.cpp) ────────────────────────────────
// Runtime settings/config now persist to internal flash (NVS, via
// Preferences) rather than the SD card -- see loadRuntimeConfig()'s comment
// in sd_store.cpp. SD is still used here for diagnostics and the boot splash.
void loadRuntimeConfig();
const char* resetReasonStr();
void logDiag(const char* event);
void showBootSplash();
void saveWifiCredsToFlash(const String& ssid, const String& password);
void saveIntConfigToFlash(const char* key, int32_t value);
// Cached last-known server IPv4 (raw 4 bytes in an int32). Not a Settings-area
// key — see the comment on loadServerIpFromFlash() in sd_store.cpp.
const char* const SERVER_IP_KEY = "server_ip";
uint32_t loadServerIpFromFlash();

// Floor on how often the poll loop touches the SD card for the JSON caches,
// /archive.csv and the capacity stats. Decoupled from Poll Interval so the 5s
// setting doesn't turn every poll into five file operations — see the comments
// at fetchUsage()'s persist block (net.cpp) and networkTask() (the .ino).
const uint32_t SD_PERSIST_MIN_MS = 60000UL;
void forgetWifiFromFlash();
