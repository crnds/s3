// AP setup (ap_setup.cpp).
#pragma once
#include <Arduino.h>

// ── AP SETUP (ap_setup.cpp) ────────────────────────────────
void runApSetup();  // blocks until configured, then ESP.restart()s — never returns
