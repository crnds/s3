// AXS15231B panel bring-up + the rotate-and-stream present path. See
// display.h for the contract and lib/axs15231b's header for why every update
// is a whole frame.
#include "display.h"
#include "pins.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <driver/spi_master.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_commands.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include "esp_lcd_axs15231b.h"

// ── PANEL GEOMETRY ────────────────────────────────────────
static const int PANEL_W = 320;   // native portrait
static const int PANEL_H = 480;
// Rows per DMA strip. 48 rows x 320 px x 2 B = 30,720 B per strip, two strips
// ping-pong (as in the seller's LVGL port, whose trans_size is hres*vres/10).
static const int STRIP_ROWS = 48;
static const int STRIP_PIXELS = PANEL_W * STRIP_ROWS;
static const int STRIP_COUNT = PANEL_H / STRIP_ROWS;  // 10

static const spi_host_device_t LCD_HOST = SPI2_HOST;
static const uint32_t LCD_PCLK_HZ = 40 * 1000 * 1000;  // seller value; faster is untested
static const ledc_channel_t BL_CHANNEL = LEDC_CHANNEL_1;
static const ledc_timer_t BL_TIMER = LEDC_TIMER_1;
// Longest we'll wait for a TE edge before presenting anyway: a panel that
// stopped pulsing must not freeze the render core.
static const uint32_t TE_WAIT_MS = 25;

// ── STATE ─────────────────────────────────────────────────
static esp_lcd_panel_io_handle_t ioHandle = nullptr;
static esp_lcd_panel_handle_t panelHandle = nullptr;
static uint16_t* strips[2] = {nullptr, nullptr};
static SemaphoreHandle_t teSem = nullptr;
static volatile uint32_t teCount = 0;
static uint32_t lastPresentUs = 0;
static uint32_t lastTeWaitUs = 0;
static uint32_t presentCount = 0;
static bool flipped = false;

// ── BRING-UP ──────────────────────────────────────────────
static void IRAM_ATTR onTeEdge(void*) {
  teCount = teCount + 1;  // sole writer; `++` on a volatile is deprecated in C++20
  BaseType_t woken = pdFALSE;
  xSemaphoreGiveFromISR(teSem, &woken);
  if (woken) portYIELD_FROM_ISR();
}

static void backlightBegin() {
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_10_BIT;
  t.timer_num = BL_TIMER;
  t.freq_hz = 5000;
  t.clk_cfg = LEDC_AUTO_CLK;
  ledc_timer_config(&t);
  ledc_channel_config_t c = {};
  c.gpio_num = S3_LCD_BL;
  c.speed_mode = LEDC_LOW_SPEED_MODE;
  c.channel = BL_CHANNEL;
  c.intr_type = LEDC_INTR_DISABLE;
  c.timer_sel = BL_TIMER;
  c.duty = 0;
  ledc_channel_config(&c);
}

// QSPI bus + the panel IO on it.
static bool panelIoBegin() {
  spi_bus_config_t bus = {};
  bus.sclk_io_num = S3_LCD_SCK;
  bus.data0_io_num = S3_LCD_D0;
  bus.data1_io_num = S3_LCD_D1;
  bus.data2_io_num = S3_LCD_D2;
  bus.data3_io_num = S3_LCD_D3;
  bus.data4_io_num = -1;
  bus.data5_io_num = -1;
  bus.data6_io_num = -1;
  bus.data7_io_num = -1;
  bus.max_transfer_sz = STRIP_PIXELS * 2 + 64;
  bus.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_QUAD;
  if (spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO) != ESP_OK) return false;

  esp_lcd_panel_io_spi_config_t io = {};
  io.cs_gpio_num = S3_LCD_CS;
  io.dc_gpio_num = -1;
  io.spi_mode = 3;
  io.pclk_hz = LCD_PCLK_HZ;
  io.trans_queue_depth = 10;
  io.lcd_cmd_bits = 32;
  io.lcd_param_bits = 8;
  io.flags.quad_mode = true;
  return esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &ioHandle) == ESP_OK;
}

static bool stripsBegin() {
  for (int i = 0; i < 2; i++) {
    strips[i] = (uint16_t*)heap_caps_malloc(STRIP_PIXELS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!strips[i]) return false;
  }
  return true;
}

// Queue strip s (panel rows s*STRIP_ROWS ..) from buf.
static void drawStrip(int s, const uint16_t* buf) {
  esp_lcd_panel_draw_bitmap(panelHandle, 0, s * STRIP_ROWS, PANEL_W, (s + 1) * STRIP_ROWS, buf);
}

// Driver, reset + init, cleared GRAM, DISPON. Needs the strips.
static bool panelBegin() {
  axs15231b_vendor_config_t vendor = {};
  vendor.init_cmds = nullptr;  // driver default = the seller BSP's table
  vendor.init_cmds_size = 0;
  vendor.flags.use_qspi_interface = 1;
  esp_lcd_panel_dev_config_t pc = {};
  pc.reset_gpio_num = -1;
  pc.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
  pc.bits_per_pixel = 16;
  pc.vendor_config = &vendor;
  if (esp_lcd_new_panel_axs15231b(ioHandle, &pc, &panelHandle) != ESP_OK) return false;

  esp_lcd_panel_reset(panelHandle);
  esp_lcd_panel_init(panelHandle);
  // GRAM powers up as random bits: clear it to black while the display is
  // still off, so DISPON never scans out noise on a cold boot.
  memset(strips[0], 0, STRIP_PIXELS * 2);
  for (int s = 0; s < STRIP_COUNT; s++) drawStrip(s, strips[0]);
  // The vendored driver's disp_on_off hook names its argument `off`, so
  // false here means DISPLAY ON (same call the seller's BSP makes).
  esp_lcd_panel_disp_on_off(panelHandle, false);
  return true;
}

static void teBegin() {
  teSem = xSemaphoreCreateBinary();
  gpio_config_t te = {};
  te.pin_bit_mask = 1ULL << S3_LCD_TE;
  te.mode = GPIO_MODE_INPUT;
  te.pull_up_en = GPIO_PULLUP_ENABLE;
  te.intr_type = GPIO_INTR_NEGEDGE;  // seller BSP syncs on the falling edge
  gpio_config(&te);
  gpio_install_isr_service(0);  // harmless if Arduino already installed it
  gpio_isr_handler_add((gpio_num_t)S3_LCD_TE, onTeEdge, nullptr);
}

bool displayBegin() {
  backlightBegin();
  if (!panelIoBegin() || !stripsBegin() || !panelBegin()) return false;
  teBegin();
  return true;
}

// ── PANEL CONTROL ─────────────────────────────────────────
void displaySetBrightness(uint8_t level) {
  uint32_t duty = ((uint32_t)level * 1023) / 255;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

// Bare panel command, QSPI-encoded the way the vendored driver's tx_param()
// does it (opcode 0x02 in the top byte, command in bits 8..15). tx_param
// drains any strip DMA still queued before it sends.
static void panelCommand(uint8_t cmd) {
  esp_lcd_panel_io_tx_param(ioHandle, (0x02 << 24) | ((int)cmd << 8), nullptr, 0);
}

void displaySetSleep(bool sleep) {
  if (!panelHandle) return;
  if (sleep) {
    // The driver's hook names its argument `off`: true = DISPOFF.
    esp_lcd_panel_disp_on_off(panelHandle, true);
    panelCommand(LCD_CMD_SLPIN);
    delay(5);
  } else {
    panelCommand(LCD_CMD_SLPOUT);
    delay(120);  // SLPOUT settle before the next command (MIPI DCS)
    esp_lcd_panel_disp_on_off(panelHandle, false);
  }
}

void displaySetFlipped(bool f) { flipped = f; }
bool displayFlipped() { return flipped; }
uint32_t displayTeCount() { return teCount; }
uint32_t displayLastPresentUs() { return lastPresentUs; }
uint32_t displayPresentCount() { return presentCount; }
uint32_t displayLastTeWaitUs() { return lastTeWaitUs; }

// ── PIXEL OPS ─────────────────────────────────────────────
static inline uint16_t swap16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

// Materials without alpha (design.md 8.4): the frames hold big-endian RGB565,
// so each op swaps to native order, halves/quarters every channel with one
// shift and mask, and swaps back.
static inline uint16_t scrimPx(uint16_t be, int level) {
  if (level <= 0) return be;
  uint16_t c = swap16(be);
  if (level == 1) c = c - ((c >> 2) & 0x39E7);   // material.scrim.light, 75%
  else c = (c >> 1) & 0x7BEF;                     // material.scrim, 50%
  return swap16(c);
}

// Reduced-motion cross-fade (design.md 12.9): 25 / 50 / 75% of `b` over `a`,
// built from the same shift-and-mask halves and quarters.
static inline uint16_t fadePx(uint16_t abe, uint16_t bbe, int level) {
  uint16_t a = swap16(abe), b = swap16(bbe), c;
  if (level == 1)      c = (uint16_t)(a - ((a >> 2) & 0x39E7) + ((b >> 2) & 0x39E7));
  else if (level == 2) c = (uint16_t)(((a >> 1) & 0x7BEF) + ((b >> 1) & 0x7BEF));
  else                 c = (uint16_t)(((a >> 2) & 0x39E7) + b - ((b >> 2) & 0x39E7));
  return swap16(c);
}

// ── PRESENT ───────────────────────────────────────────────
// One present's worth of source description, shared by every strip.
enum PresentMode : uint8_t { PM_PLAIN, PM_SLIDE_H, PM_SHEET, PM_FADE };
struct PresentSrc {
  PresentMode mode;
  const uint16_t* a;   // the frame; the outgoing frame (slide/fade); the page behind (sheet)
  const uint16_t* b;   // incoming frame (slide/fade) or the sheet, else nullptr
  int offset;          // slide: 0..SCREEN_W; sheet: visible sheet height (may overshoot 320)
  bool forward;
  int level;           // sheet: scrim step on `a` (0..2); fade: 1..3 = 25/50/75% of `b`
  int shiftX, shiftY;
  uint16_t bg;         // byte-swapped
  int pinY0;           // slide only: rows >= this show `b` unmoved (SCREEN_H = no pin)
};

// The fields every mode sets; callers fill in the mode-specific rest.
static PresentSrc presentBase(PresentMode mode, const uint16_t* a, const uint16_t* b,
                              int shiftX, int shiftY, uint16_t bg) {
  PresentSrc p = {};
  p.mode = mode;
  p.a = a;
  p.b = b;
  p.forward = true;
  p.shiftX = shiftX;
  p.shiftY = shiftY;
  p.bg = swap16(bg);
  p.pinY0 = SCREEN_H;
  return p;
}

// Strip rows [i0, i1) of one panel column; consecutive rows are PANEL_W apart.
static inline void fillRows(uint16_t* d, int i0, int i1, uint16_t v) {
  for (int i = i0; i < i1; i++) d[i * PANEL_W] = v;
}

// Fill one portrait strip (panel rows py0..py0+STRIP_ROWS-1) from the
// landscape source. Walks the SOURCE row-major (ly outer, a short contiguous
// run of lx inner) because the frames live in PSRAM, where strided column
// reads would miss the cache on nearly every pixel.
//   normal  (90deg):  panel (px,py) <- landscape (lx=py,      ly=319-px)
//   flipped (270deg): panel (px,py) <- landscape (lx=479-py,  ly=px)
// Each mode gets its own inner loop (no per-pixel call or switch): the source
// row is chosen once per ly, and sx steps by one.
static void fillStrip(uint16_t* dst, const PresentSrc& p, int py0) {
  // Source x for strip row i: normal lx = py0+i, flipped lx = 479-py0-i;
  // sx = lx - shiftX. The pixel shift only offsets the source and exposes a bg
  // margin, so which rows [lo, hi) land inside the frame (0 <= sx < SCREEN_W)
  // is the same for every ly: worked out once per strip, not per pixel.
  const int step = flipped ? -1 : 1;
  const int sx0 = (flipped ? (SCREEN_W - 1 - py0) : py0) - p.shiftX;  // sx at i = 0
  auto outside = [&](int i) { int sx = sx0 + step * i; return sx < 0 || sx >= SCREEN_W; };
  int lo = 0, hi = STRIP_ROWS;
  while (lo < hi && outside(lo)) lo++;
  while (hi > lo && outside(hi - 1)) hi--;
  const int sxLo = sx0 + step * lo;

  for (int ly = 0; ly < SCREEN_H; ly++) {
    uint16_t* d = dst + (flipped ? ly : (PANEL_W - 1 - ly));
    const int sy = ly - p.shiftY;
    if (sy < 0 || sy >= SCREEN_H) {
      fillRows(d, 0, STRIP_ROWS, p.bg);
      continue;
    }
    fillRows(d, 0, lo, p.bg);
    fillRows(d, hi, STRIP_ROWS, p.bg);
    const uint16_t* ra = p.a + sy * SCREEN_W;
    int sx = sxLo;
    switch (p.mode) {
      case PM_PLAIN:
        for (int i = lo; i < hi; i++, sx += step) d[i * PANEL_W] = ra[sx];
        break;
      case PM_SLIDE_H: {
        const uint16_t* rb = p.b + sy * SCREEN_W;
        const int off = p.offset, split = SCREEN_W - off;
        if (ly >= p.pinY0) {
          // Pinned band (design.md 7's status strip): always `b`, unmoved, so
          // it doesn't slide with the page above it.
          for (int i = lo; i < hi; i++, sx += step) d[i * PANEL_W] = rb[sx];
        } else if (p.forward) {
          // Outgoing page shifted left by `off`, incoming fills the right.
          for (int i = lo; i < hi; i++, sx += step)
            d[i * PANEL_W] = sx < split ? ra[sx + off] : rb[sx - split];
        } else {
          for (int i = lo; i < hi; i++, sx += step)
            d[i * PANEL_W] = sx < off ? rb[sx + split] : ra[sx - off];
        }
        break;
      }
      case PM_SHEET: {
        const int top = SCREEN_H - p.offset;  // the sheet's top edge on screen
        if (sy < top) {
          for (int i = lo; i < hi; i++, sx += step) d[i * PANEL_W] = scrimPx(ra[sx], p.level);
        } else if (sy - top < SCREEN_H) {
          const uint16_t* rs = p.b + (sy - top) * SCREEN_W;
          for (int i = lo; i < hi; i++, sx += step) d[i * PANEL_W] = rs[sx];
        } else {
          fillRows(d, lo, hi, p.bg);  // overshoot: bg below the sheet
        }
        break;
      }
      case PM_FADE: {
        const uint16_t* rb = p.b + sy * SCREEN_W;
        for (int i = lo; i < hi; i++, sx += step) d[i * PANEL_W] = fadePx(ra[sx], rb[sx], p.level);
        break;
      }
    }
  }
}

static void presentSrc(const PresentSrc& p) {
  if (!panelHandle || !p.a) return;
  uint32_t t0 = micros();

  // Build the first strip before waiting on TE, so the transfer can start the
  // moment the edge arrives.
  fillStrip(strips[0], p, 0);

  uint32_t tw = micros();
  xSemaphoreTake(teSem, 0);                          // drop a stale edge
  xSemaphoreTake(teSem, pdMS_TO_TICKS(TE_WAIT_MS));  // start on a fresh one
  lastTeWaitUs = micros() - tw;

  for (int s = 0; s < STRIP_COUNT; s++) {
    // draw_bitmap blocks in its CASET param write until the previous strip's
    // DMA has drained, so by the time strip s+1 is filled into the other
    // buffer, strip s-1 (which used it) is guaranteed done. No extra fences.
    drawStrip(s, strips[s & 1]);
    if (s + 1 < STRIP_COUNT) fillStrip(strips[(s + 1) & 1], p, (s + 1) * STRIP_ROWS);
  }
  lastPresentUs = micros() - t0;
  presentCount++;
}

void displayPresent(const uint16_t* frame, int shiftX, int shiftY, uint16_t bg) {
  presentSrc(presentBase(PM_PLAIN, frame, nullptr, shiftX, shiftY, bg));
}

void displayPresentSlide(const uint16_t* from, const uint16_t* to, int offset, bool forward,
                         int shiftX, int shiftY, uint16_t bg, int pinY0) {
  PresentSrc p = presentBase(PM_SLIDE_H, from, to, shiftX, shiftY, bg);
  p.offset = constrain(offset, 0, SCREEN_W);
  p.forward = forward;
  p.pinY0 = pinY0;
  presentSrc(p);
}

void displayPresentSheet(const uint16_t* behind, const uint16_t* sheet, int visibleH, int scrimLevel,
                         int shiftX, int shiftY, uint16_t bg) {
  PresentSrc p = presentBase(PM_SHEET, behind, sheet, shiftX, shiftY, bg);
  p.offset = max(visibleH, 0);
  p.level = scrimLevel;
  presentSrc(p);
}

void displayPresentFade(const uint16_t* from, const uint16_t* to, int level,
                        int shiftX, int shiftY, uint16_t bg) {
  PresentSrc p = presentBase(PM_FADE, from, to, shiftX, shiftY, bg);
  p.level = constrain(level, 1, 3);
  presentSrc(p);
}
