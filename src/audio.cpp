// Speaker engine. One FreeRTOS task on core 0 owns the I2S channel and streams
// the active clip in 128-frame chunks; idle time is DMA silence (auto_clear),
// so the always-on amp never sees a torn-down or underrun channel.
#include <Arduino.h>
#include <driver/i2s_std.h>
#include <esp_heap_caps.h>
#include <math.h>
#include "audio.h"
#include "pins.h"
#include "state.h"

// ── CONSTANTS ──────────────────────────────────────────────
static const uint32_t SAMPLE_RATE = 16000;
static const int CHUNK_FRAMES = 128;         // 8 ms: also the tap-to-sound latency floor
static const float PEAK = 0.9f;              // normalised clip peak, before the volume gain

// Linear gain per volume level (index 0..SOUND_VOL_MAX): Off, then ~4.5 dB per
// step from 0.04 to 0.80. The default (3) is a barely-there desk-device tock.
// Tune on the board.
static const float VOLUME_GAIN[SOUND_VOL_MAX + 1] = {0.0f, 0.040f, 0.066f, 0.109f, 0.18f, 0.30f, 0.49f, 0.80f};

// Higher priority preempts lower; equal restarts (rapid taps retrigger).
static const uint8_t PRIORITY[SND_COUNT] = {0, 2, 2, 2, 1};

// ── STATE ──────────────────────────────────────────────────
struct Clip {
  int16_t* pcm = nullptr;
  uint32_t len = 0;
};
static Clip clips[SND_COUNT];
static i2s_chan_handle_t txChan = nullptr;
static QueueHandle_t requestQueue = nullptr;
static bool audioOk = false;

// ── SYNTHESIS ──────────────────────────────────────────────
static float* newScratch(uint32_t len) {
  float* f = (float*)heap_caps_calloc(len, sizeof(float), MALLOC_CAP_SPIRAM);
  return f ? f : (float*)calloc(len, sizeof(float));
}

// Struck-bell partial: decaying sine + two upper partials, 2 ms attack ramp.
static void addBell(float* buf, uint32_t len, uint32_t startMs, float freq, float decayMs, float amp) {
  uint32_t s0 = startMs * SAMPLE_RATE / 1000;
  float tau = decayMs / 1000.0f;
  for (uint32_t i = s0; i < len; i++) {
    float t = (float)(i - s0) / SAMPLE_RATE;
    float env = expf(-t / tau);
    if (env < 0.0005f) break;
    float att = t < 0.002f ? t / 0.002f : 1.0f;
    float w = 2.0f * (float)M_PI * freq * t;
    buf[i] += amp * att * env * (sinf(w) + 0.35f * sinf(2.0f * w) + 0.12f * sinf(3.01f * w));
  }
}

// Wood "tock": a sine that drops fast in pitch with a very short decay.
static void addTock(float* buf, uint32_t len) {
  float phase = 0;
  for (uint32_t i = 0; i < len; i++) {
    float t = (float)i / SAMPLE_RATE;
    float freq = 280.0f + 220.0f * expf(-t / 0.008f);
    phase += 2.0f * (float)M_PI * freq / SAMPLE_RATE;
    float att = t < 0.0005f ? t / 0.0005f : 1.0f;
    buf[i] = att * expf(-t / 0.014f) * (sinf(phase) + 0.25f * sinf(2.4f * phase));
  }
}

// Normalise to PEAK, force a 5 ms linear fade-out so every clip ends at
// exactly zero (a non-zero last sample is an audible click on a hot amp).
static void commitClip(SoundId id, float* buf, uint32_t len) {
  float mx = 0.0001f;
  for (uint32_t i = 0; i < len; i++) mx = fmaxf(mx, fabsf(buf[i]));
  uint32_t fade = SAMPLE_RATE / 200;
  int16_t* pcm = (int16_t*)heap_caps_malloc(len * sizeof(int16_t), MALLOC_CAP_SPIRAM);
  if (!pcm) pcm = (int16_t*)malloc(len * sizeof(int16_t));
  if (pcm) {
    for (uint32_t i = 0; i < len; i++) {
      float v = buf[i] / mx * PEAK;
      if (i + fade >= len) v *= (float)(len - 1 - i) / fade;
      pcm[i] = (int16_t)(v * 32767.0f);
    }
    clips[id].pcm = pcm;
    clips[id].len = len;
  }
  free(buf);
}

static void renderClips() {
  struct Spec { SoundId id; uint32_t ms; };
  const Spec specs[] = {{SND_TOCK, 70}, {SND_DONE, 650}, {SND_ATTN, 550}, {SND_ALERT, 900}, {SND_HOURLY, 1500}};
  for (const Spec& sp : specs) {
    uint32_t len = sp.ms * SAMPLE_RATE / 1000;
    float* b = newScratch(len);
    if (!b) continue;
    switch (sp.id) {
      case SND_TOCK:   addTock(b, len); break;
      case SND_DONE:   // soft rising pair, E5 -> A5
        addBell(b, len, 0, 659.0f, 90.0f, 1.0f);
        addBell(b, len, 150, 880.0f, 130.0f, 1.0f);
        break;
      case SND_ATTN:   // double ping on A5
        addBell(b, len, 0, 880.0f, 70.0f, 1.0f);
        addBell(b, len, 170, 880.0f, 110.0f, 1.0f);
        break;
      case SND_ALERT:  // descending triad G5 E5 C5
        addBell(b, len, 0, 784.0f, 100.0f, 1.0f);
        addBell(b, len, 200, 659.0f, 100.0f, 1.0f);
        addBell(b, len, 400, 523.0f, 140.0f, 1.0f);
        break;
      case SND_HOURLY: // rising arpeggio C5 E5 G5, ringing out
        addBell(b, len, 0, 523.0f, 200.0f, 1.0f);
        addBell(b, len, 260, 659.0f, 200.0f, 1.0f);
        addBell(b, len, 520, 784.0f, 260.0f, 1.0f);
        break;
      default: break;
    }
    commitClip(sp.id, b, len);
  }
}

// ── PLAYBACK TASK ──────────────────────────────────────────
static void audioTask(void*) {
  static int16_t out[CHUNK_FRAMES * 2];  // stereo frames: the mono sample twice
  const Clip* cur = nullptr;
  SoundId curId = SND_TOCK;
  uint32_t pos = 0;
  for (;;) {
    SoundId req;
    // Idle: sleep until a request. Playing: just peek, never wait.
    while (xQueueReceive(requestQueue, &req, cur ? 0 : portMAX_DELAY) == pdTRUE) {
      if (!clips[req].pcm) continue;
      if (!cur || PRIORITY[req] >= PRIORITY[curId]) {
        cur = &clips[req];
        curId = req;
        pos = 0;
      }
      if (cur) break;
    }
    if (!cur) continue;

    int level = constrain(curId == SND_TOCK ? cfgTapVol : cfgAlertVol, 0, SOUND_VOL_MAX);
    float gain = VOLUME_GAIN[level];
    for (int i = 0; i < CHUNK_FRAMES; i++) {
      int16_t s = 0;
      if (pos < cur->len) s = (int16_t)(cur->pcm[pos++] * gain);
      out[2 * i] = out[2 * i + 1] = s;
    }
    size_t written = 0;
    i2s_channel_write(txChan, out, sizeof(out), &written, 100);
    if (pos >= cur->len) cur = nullptr;  // the chunk above carried the zero tail
  }
}

// ── PUBLIC API ─────────────────────────────────────────────
void audioInit() {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.auto_clear = true;          // DMA outputs zeros whenever we have nothing queued
  chan.dma_desc_num = 4;
  chan.dma_frame_num = CHUNK_FRAMES;
  if (i2s_new_channel(&chan, &txChan, nullptr) != ESP_OK) {
    Serial.println("[audio] i2s_new_channel FAILED");
    return;
  }
  i2s_std_config_t cfg = {};
  cfg.clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(SAMPLE_RATE);
  cfg.slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO);
  cfg.gpio_cfg.mclk = I2S_GPIO_UNUSED;
  cfg.gpio_cfg.bclk = (gpio_num_t)S3_I2S_BCLK;
  cfg.gpio_cfg.ws = (gpio_num_t)S3_I2S_LRCLK;
  cfg.gpio_cfg.dout = (gpio_num_t)S3_I2S_DIN;
  cfg.gpio_cfg.din = I2S_GPIO_UNUSED;
  if (i2s_channel_init_std_mode(txChan, &cfg) != ESP_OK || i2s_channel_enable(txChan) != ESP_OK) {
    Serial.println("[audio] i2s init/enable FAILED");
    return;
  }
  renderClips();
  requestQueue = xQueueCreate(4, sizeof(SoundId));
  xTaskCreatePinnedToCore(audioTask, "audio", 4096, nullptr, 3, nullptr, 0);
  audioOk = true;
  Serial.printf("[audio] ready, %lu Hz, tap=%d alert=%d\n", (unsigned long)SAMPLE_RATE, cfgTapVol, cfgAlertVol);
}

// The tock has its own volume; every other sound shares the alert volume.
static int volumeFor(SoundId id) { return id == SND_TOCK ? cfgTapVol : cfgAlertVol; }

static bool categoryOn(SoundId id) {
  switch (id) {
    case SND_HOURLY: return cfgHourlyChime;
    case SND_DONE:
    case SND_ATTN:   return cfgClaudeDing;
    case SND_ALERT:  return cfgSoundAlerts;
    default:         return true;
  }
}

void audioPlay(SoundId id) {
  if (!audioOk || id >= SND_COUNT) return;
  if (volumeFor(id) == SOUND_VOL_OFF || !categoryOn(id)) return;
  if ((screenSleeping && cfgMuteSleep) || (nightDimActive && cfgMuteNight)) return;
  xQueueSend(requestQueue, &id, 0);  // full queue: drop (a burst of taps needs only the latest)
}

void audioTest(SoundId id) {
  if (!audioOk || id >= SND_COUNT || volumeFor(id) == SOUND_VOL_OFF) return;
  xQueueSend(requestQueue, &id, 0);
}
