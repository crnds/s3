#pragma once
// Speaker output (design.md 12.10): the NS4168 I2S amp, one voice, short clips
// synthesised at boot. Firmware-only -- a deliberate non-twin (the simulator
// shows the Settings rows and stays silent).
#include <stdint.h>

enum SoundId : uint8_t {
  SND_TOCK = 0,   // a committed tap: low-key wood "tock"
  SND_DONE,       // Claude Code finished
  SND_ATTN,       // Claude Code needs you
  SND_ALERT,      // usage / BTC / connectivity alert
  SND_HOURLY,     // hh:00:00 chime
  SND_COUNT
};

// Two independent volumes, both 0 = Off, 1..7 in ~4.5 dB steps: the tap tock
// ("tap_level") and everything else -- chime, Claude ding, alerts ("sound_level").
enum { SOUND_VOL_OFF = 0, SOUND_VOL_MAX = 7, TAP_VOL_DEFAULT = 2, ALERT_VOL_DEFAULT = 4 };

// Install the I2S channel (kept running for good: the amp is always on) and
// render the clips. Call once from setup(), after PSRAM is up.
void audioInit();

// Request a sound. Never blocks and never touches I2S, so it is safe from the
// touch/render path on any core. Dropped when its volume is Off (tap volume for
// the tock, alert volume for the rest), its category is switched off (hourly
// chime / Claude ding / alerts), a mute rule applies
// (screen asleep, night mode), or a higher-priority sound is playing (alerts
// and dings > chime > tock).
void audioPlay(SoundId id);

// Settings > Sound > Test sound: plays regardless of category toggles and mute
// rules, so the user hears exactly what the volume does. Still silent when that
// sound's own volume is Off.
void audioTest(SoundId id);
