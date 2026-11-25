// OTA updates (ota.cpp, roadmap O9).
#pragma once
#include <Arduino.h>

// ── OTA UPDATES (ota.cpp, roadmap O9) ──────────────────────
// The Mac offers a published build in /api/usage; networkTask (core 0) does
// all of it, Settings > Firmware only reads otaState / otaProgress and sets
// pendingOtaInstall. cfgOtaAuto ("ota_auto", default on): install a newly
// published build by itself.
enum OtaState : uint8_t { OTA_NONE, OTA_CURRENT, OTA_AVAILABLE, OTA_INSTALLING, OTA_FAILED, OTA_ROLLED_BACK };
extern volatile OtaState otaState;
extern volatile int otaProgress;  // 0..100 while OTA_INSTALLING
extern volatile bool pendingOtaInstall;
extern bool cfgOtaAuto;
void otaBoot();
void otaNoteOffer(const char* sha256, int size, uint32_t installId);
const char* otaRunningSha();  // for the X-S3-Firmware poll header
const char* otaReport();      // X-S3-OTA: "<state>; auto=<0|1>"
void otaAfterPoll(bool connected);
void otaInstall();
