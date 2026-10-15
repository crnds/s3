// OTA updates over the LAN (roadmap O9). `pio run -t publish` copies a build to
// the Mac (~/.cyd_firmware/s3.bin); ~/cyd's usage_server announces it in every
// /api/usage as firmware.s3.{sha256,size} and serves it at /firmware/s3.bin.
//
// The sha256 is the image digest esptool appends to every build -- the same
// value esp_partition_get_sha256() returns for the running app partition -- so
// "offered == running" means up to date with no version numbers to keep.
//
// Everything here runs on networkTask (core 0): the download goes into the idle
// app slot of default_16MB.csv's two 6.25MB slots, then the board restarts into
// it. A new build boots ESP_OTA_IMG_PENDING_VERIFY and is confirmed only by its
// first good live poll (verifyRollbackLater() keeps Arduino from confirming it
// at boot). A crash or reboot before that, or OTA_VERIFY_TIMEOUT_MS without a
// good poll, and the bootloader goes back to the previous build.
//
// NVS namespace "s3ota" (written here only):
//   seen  the last offered build auto update acted on. Auto installs only a
//         build it hasn't seen, so a dev build flashed over USB isn't replaced
//         by an older published one -- that shows "Update ready" instead.
//   try   the build being installed, until a good poll confirms it.
//   bad   a build that never got confirmed (rolled back); auto skips it.
//   req   the last install request from the Mac's control panel acted on.
//
// Every poll reports the running build and this state to the server
// (X-S3-Firmware / X-S3-OTA headers, otaRunningSha() / otaReport()), which is
// what the control panel shows. Its "Install on board" puts an install id in
// the offer (firmware.s3.install): acted on once per id, whatever Auto update
// says and even for a build auto skipped or that rolled back -- and since the
// id is remembered across restarts, a build that rolls back can't loop.
#include "state.h"
#include <Update.h>
#include "esp_ota_ops.h"

bool cfgOtaAuto = true;
volatile OtaState otaState = OTA_NONE;
volatile int otaProgress = 0;
volatile bool pendingOtaInstall = false;

static const char* const OTA_NVS = "s3ota";
static const uint32_t OTA_VERIFY_TIMEOUT_MS = 10UL * 60 * 1000;
static const uint32_t OTA_STALL_MS = 10000;  // no bytes for this long = this attempt failed
static const int OTA_ATTEMPTS = 3;            // weak WiFi stalls a 3MB download now and then

static char runningSha[65] = "";
static char offeredSha[65] = "";
static int offeredSize = 0;
static char seenSha[65] = "", badSha[65] = "", failedSha[65] = "";
static uint32_t offeredInstallId = 0;  // firmware.s3.install, 0 = no request
static uint32_t lastInstallReq = 0;    // NVS "req"
static bool pendingVerify = false;
static uint32_t verifyStartMs = 0;

// Arduino's initArduino() confirms a freshly installed image at boot unless
// this returns true. The first good poll confirms it instead (otaAfterPoll).
// extern "C": the core's weak default (esp32-hal-misc.c) is a C symbol, and a
// C++-mangled definition would silently leave it in place.
extern "C" bool verifyRollbackLater() { return true; }

static bool partitionSha(const esp_partition_t* p, char* out) {
  uint8_t sha[32];
  out[0] = '\0';
  if (!p || esp_partition_get_sha256(p, sha) != ESP_OK) return false;
  for (int i = 0; i < 32; i++) snprintf(out + i * 2, 3, "%02x", sha[i]);
  return true;
}

static void nvsPut(const char* key, const char* value) {
  Preferences p;
  p.begin(OTA_NVS, false);
  if (value) p.putString(key, value);
  else p.remove(key);
  p.end();
}

// Before the first poll: which build is running, is it still on probation,
// and did the last install roll back.
void otaBoot() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  partitionSha(running, runningSha);
  esp_ota_img_states_t st;
  pendingVerify = esp_ota_get_state_partition(running, &st) == ESP_OK && st == ESP_OTA_IMG_PENDING_VERIFY;
  verifyStartMs = millis();

  Preferences p;
  p.begin(OTA_NVS, false);
  strlcpy(seenSha, p.getString("seen", "").c_str(), sizeof(seenSha));
  strlcpy(badSha, p.getString("bad", "").c_str(), sizeof(badSha));
  lastInstallReq = p.getUInt("req", 0);
  String tried = p.getString("try", "");
  if (tried.length() && tried != runningSha) {
    // The last install was never confirmed and we're not running it: it
    // rolled back. Auto update won't install it again.
    strlcpy(badSha, tried.c_str(), sizeof(badSha));
    p.putString("bad", badSha);
    p.remove("try");
    logDiag("ota_rolled_back");
  }
  p.end();
  Serial.printf("[ota] running %.12s on %s (%s)\n", runningSha, running ? running->label : "?",
                pendingVerify ? "pending verify" : "confirmed");
}

// applyUsageDoc(), live polls only: the build the Mac offers ("" = none) and
// a pending install request for it (0 = none).
void otaNoteOffer(const char* sha256, int size, uint32_t installId) {
  bool valid = strlen(sha256) == 64;
  for (int i = 0; valid && i < 64; i++) valid = isxdigit((unsigned char)sha256[i]);
  strlcpy(offeredSha, valid ? sha256 : "", sizeof(offeredSha));
  offeredSize = valid ? size : 0;
  offeredInstallId = valid ? installId : 0;
}

const char* otaRunningSha() { return runningSha; }

// "<state>; auto=<0|1>; req=<id>" for the X-S3-OTA poll header. req is the
// last install request acted on, so the server can drop a request that failed
// instead of showing it pending.
const char* otaReport() {
  static const char* const NAMES[] = {"none", "current", "available", "installing", "failed", "rolled-back"};
  static char buf[48];
  snprintf(buf, sizeof(buf), "%s; auto=%d; req=%lu", NAMES[otaState], cfgOtaAuto ? 1 : 0,
           (unsigned long)lastInstallReq);
  return buf;
}

// One download into the idle slot; true when it is complete and valid.
static bool otaDownload(const String& url, int& code, int& done, int& len) {
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(OTA_STALL_MS);
  http.begin(url);
  code = http.GET();
  len = code == 200 ? http.getSize() : -1;
  done = 0;
  bool ok = len > 0 && (offeredSize <= 0 || len == offeredSize) && Update.begin(len, U_FLASH);
  if (ok) {
    WiFiClient* s = http.getStreamPtr();
    static uint8_t buf[4096];
    uint32_t lastDataMs = millis();
    while (done < len && millis() - lastDataMs < OTA_STALL_MS) {
      int avail = s->available();
      if (avail <= 0) {
        delay(2);
        continue;
      }
      int n = s->read(buf, min(avail, (int)sizeof(buf)));
      if (n <= 0) continue;
      if (Update.write(buf, n) != (size_t)n) break;
      done += n;
      lastDataMs = millis();
      otaProgress = (int)((int64_t)done * 100 / len);
      vTaskDelay(1);  // let core 0's idle task run between flash writes
    }
    // Update.end() runs esp_ota_end (image checksum + appended SHA-256) and
    // makes the new slot the boot partition.
    ok = done == len && Update.end();
    if (!ok) Update.abort();
  }
  http.end();
  return ok;
}

static OtaState currentState() {
  if (!offeredSha[0]) return OTA_NONE;
  if (!strcmp(offeredSha, runningSha)) return OTA_CURRENT;
  if (!strcmp(offeredSha, badSha)) return OTA_ROLLED_BACK;
  if (!strcmp(offeredSha, failedSha)) return OTA_FAILED;
  return OTA_AVAILABLE;
}

// After every poll: confirm a new build on its first good poll (or give up on
// it), refresh the state Settings shows, and auto-install a new publish.
void otaAfterPoll(bool connected) {
  if (pendingVerify) {
    if (connected) {
      esp_ota_mark_app_valid_cancel_rollback();
      pendingVerify = false;
      nvsPut("try", nullptr);
      logDiag("ota_confirmed");
      Serial.println("[ota] new build confirmed by a good poll");
    } else if (millis() - verifyStartMs >= OTA_VERIFY_TIMEOUT_MS) {
      logDiag("ota_verify_timeout");
      esp_ota_mark_app_invalid_rollback_and_reboot();
    }
  }
  if (otaState == OTA_INSTALLING) return;
  otaState = currentState();
  if (connected && offeredInstallId && offeredInstallId != lastInstallReq) {
    // Once per request id, remembered before installing (an install restarts).
    lastInstallReq = offeredInstallId;
    Preferences p;
    p.begin(OTA_NVS, false);
    p.putUInt("req", lastInstallReq);
    p.end();
    if (otaState == OTA_AVAILABLE || otaState == OTA_FAILED || otaState == OTA_ROLLED_BACK) {
      logDiag("ota_requested");
      strlcpy(seenSha, offeredSha, sizeof(seenSha));
      nvsPut("seen", seenSha);
      otaInstall();
      return;
    }
  }
  if (connected && cfgOtaAuto && otaState == OTA_AVAILABLE && !pendingVerify && strcmp(offeredSha, seenSha) != 0) {
    // Once per publish, even if this attempt fails ("Update now" retries).
    strlcpy(seenSha, offeredSha, sizeof(seenSha));
    nvsPut("seen", seenSha);
    otaInstall();
  }
}

// Download the offered build into the idle slot, check it is that build, and
// restart into it. Returns only on failure (otaState = OTA_FAILED).
void otaInstall() {
  pendingOtaInstall = false;
  otaState = currentState();
  if (otaState == OTA_NONE || otaState == OTA_CURRENT) return;
  char target[65];
  strlcpy(target, offeredSha, sizeof(target));
  String url;
  if (!serverUrl("/firmware/s3.bin", url)) {
    strlcpy(failedSha, target, sizeof(failedSha));
    otaState = OTA_FAILED;
    return;
  }
  otaProgress = 0;
  otaState = OTA_INSTALLING;
  Serial.printf("[ota] installing %.12s from %s\n", target, url.c_str());
  logDiag("ota_start");

  int code = 0, done = 0, len = 0;
  bool ok = false;
  for (int attempt = 1; attempt <= OTA_ATTEMPTS && !ok; attempt++) {
    otaProgress = 0;
    ok = otaDownload(url, code, done, len);
    if (!ok) Serial.printf("[ota] attempt %d failed: http %d, %d/%d bytes, %s\n", attempt, code, done, len,
                           Update.errorString());
  }

  // The slot must hold the announced build, not a file republished while it
  // downloaded.
  if (ok) {
    char got[65];
    partitionSha(esp_ota_get_boot_partition(), got);
    if (strcmp(got, target) != 0) {
      esp_ota_set_boot_partition(esp_ota_get_running_partition());
      ok = false;
    }
  }
  if (!ok) {
    Serial.printf("[ota] failed: http %d, %d/%d bytes, %s\n", code, done, len, Update.errorString());
    logDiag((String("ota_failed http=") + code + " bytes=" + done + "/" + len).c_str());
    strlcpy(failedSha, target, sizeof(failedSha));
    otaState = OTA_FAILED;
    return;
  }
  nvsPut("try", target);
  nvsPut("seen", target);
  otaProgress = 100;
  logDiag("ota_installed");
  Serial.println("[ota] installed, restarting");
  delay(200);
  ESP.restart();
}
