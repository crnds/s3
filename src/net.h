// Network (net.cpp).
#pragma once
#include <Arduino.h>

// "http://<server ip>:<port><path>" for the resolved Mac (net.cpp); false
// before the server has ever resolved.
bool serverUrl(const char* path, String& out);

// ── NETWORK (net.cpp) ──────────────────────────────────────
void ensureMdns();
void connectWifi();
bool resolveServer();
bool fetchUsage();
bool loadCachedUsage();
void loadEnvCache();
void loadWeatherCache();
// fromNetwork=true only for a live /api/usage fetch — applies power.battery_save
// into serverBatterySave. SD cache loads pass false so a stale Mac power flag
// can't floor the poll across an overnight offline boot.
bool applyUsageJson(const String& payload, bool fromNetwork = false);
// Set false by networkTask() (main.cpp) on WiFi loss, so ensureMdns()
// re-initializes once WiFi returns.
extern bool mdnsStarted;
