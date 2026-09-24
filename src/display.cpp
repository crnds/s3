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

static esp_lcd_panel_io_handle_t ioHandle = nullptr;
static esp_lcd_panel_handle_t panelHandle = nullptr;
static uint16_t* strips[2] = {nullptr, nullptr};
static SemaphoreHandle_t teSem = nullptr;
static volatile uint32_t teCount = 0;
static uint32_t lastPresentUs = 0;
static uint32_t presentCount = 0;
static bool flipped = false;
static bool invertWanted = false;
static bool invertApplied = false;

static void IRAM_ATTR onTeEdge(void*) {
  teCount++;
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

void displaySetBrightness(uint8_t level) {
  uint32_t duty = ((uint32_t)level * 1023) / 255;
  ledc_set_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, BL_CHANNEL);
}

bool displayBegin() {
  backlightBegin();

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
  if (esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io, &ioHandle) != ESP_OK) return false;

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
  // The vendored driver's disp_on_off hook names its argument `off`, so
  // false here means DISPLAY ON (same call the seller's BSP makes).
  esp_lcd_panel_disp_on_off(panelHandle, false);

  for (int i = 0; i < 2; i++) {
    strips[i] = (uint16_t*)heap_caps_malloc(STRIP_PIXELS * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    if (!strips[i]) return false;
  }

  teSem = xSemaphoreCreateBinary();
  gpio_config_t te = {};
  te.pin_bit_mask = 1ULL << S3_LCD_TE;
  te.mode = GPIO_MODE_INPUT;
  te.pull_up_en = GPIO_PULLUP_ENABLE;
  te.intr_type = GPIO_INTR_NEGEDGE;  // seller BSP syncs on the falling edge
  gpio_config(&te);
  gpio_install_isr_service(0);  // harmless if Arduino already installed it
  gpio_isr_handler_add((gpio_num_t)S3_LCD_TE, onTeEdge, nullptr);
  return true;
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

void displaySetInvert(bool on) { invertWanted = on; }
void displaySetFlipped(bool f) { flipped = f; }
bool displayFlipped() { return flipped; }
uint32_t displayTeCount() { return teCount; }
uint32_t displayLastPresentUs() { return lastPresentUs; }
uint32_t displayPresentCount() { return presentCount; }

// One present's worth of source description, shared by every strip.
struct PresentSrc {
  const uint16_t* a;   // the frame (or the outgoing frame, during a slide)
  const uint16_t* b;   // incoming frame during a slide, else nullptr
  int offset;          // slide progress, 0..SCREEN_W
  bool forward;
  int shiftX, shiftY;
  uint16_t bg;         // byte-swapped
  int border;          // border overlay thickness, 0 = none
  uint16_t borderCol;  // byte-swapped
};

static int borderThickness = 0;
static uint16_t borderColor = 0xFFFF;
static uint32_t lastTeWaitUs = 0;

void displaySetBorder(int thickness, uint16_t color) {
  borderThickness = thickness;
  borderColor = color;
}

// Landscape pixel shown at screen (lx, ly), after pixel shift + slide.
static inline uint16_t srcPixel(const PresentSrc& p, int lx, int ly) {
  if (p.border && (lx < p.border || lx >= SCREEN_W - p.border || ly < p.border || ly >= SCREEN_H - p.border))
    return p.borderCol;
  int sx = lx - p.shiftX, sy = ly - p.shiftY;
  if (sx < 0 || sx >= SCREEN_W || sy < 0 || sy >= SCREEN_H) return p.bg;
  if (!p.b) return p.a[sy * SCREEN_W + sx];
  if (p.forward) {
    int split = SCREEN_W - p.offset;  // outgoing page shifted left by offset
    return sx < split ? p.a[sy * SCREEN_W + sx + p.offset] : p.b[sy * SCREEN_W + sx - split];
  }
  return sx < p.offset ? p.b[sy * SCREEN_W + sx + SCREEN_W - p.offset] : p.a[sy * SCREEN_W + sx - p.offset];
}

// Fill one portrait strip (panel rows py0..py0+STRIP_ROWS-1) from the
// landscape source. Walks the SOURCE row-major (ly outer, a short contiguous
// run of lx inner) because the frames live in PSRAM, where strided column
// reads would miss the cache on nearly every pixel.
//   normal  (90deg):  panel (px,py) <- landscape (lx=py,      ly=319-px)
//   flipped (270deg): panel (px,py) <- landscape (lx=479-py,  ly=px)
// The plain present (no slide, no border, no shift) takes a tight fast path.
static void fillStrip(uint16_t* dst, const PresentSrc& p, int py0) {
  // Fast path: a single frame, no border -- the normal present, with or
  // without the pixel-shift orbit. The shift only offsets the source and
  // exposes a bg margin; which strip rows (i) land inside the frame is the
  // same for every ly, so it's worked out once per strip, not per pixel.
  if (!p.b && !p.border) {
    // Source x for strip row i: normal lx = py0+i, flipped lx = 479-py0-i;
    // sx = lx - shiftX. Keep i where 0 <= sx < SCREEN_W.
    int iLo = 0, iHi = STRIP_ROWS;  // valid i range [iLo, iHi)
    int sx0 = (flipped ? (SCREEN_W - 1 - py0) : py0) - p.shiftX;  // sx at i = 0
    int step = flipped ? -1 : 1;
    while (iLo < iHi && (sx0 + step * iLo < 0 || sx0 + step * iLo >= SCREEN_W)) iLo++;
    while (iHi > iLo && (sx0 + step * (iHi - 1) < 0 || sx0 + step * (iHi - 1) >= SCREEN_W)) iHi--;
    for (int ly = 0; ly < SCREEN_H; ly++) {
      int px = flipped ? ly : (PANEL_W - 1 - ly);
      uint16_t* d = dst + px;
      int sy = ly - p.shiftY;
      if (sy < 0 || sy >= SCREEN_H) {
        for (int i = 0; i < STRIP_ROWS; i++) d[i * PANEL_W] = p.bg;
        continue;
      }
      const uint16_t* s = p.a + sy * SCREEN_W + sx0;
      for (int i = 0; i < iLo; i++) d[i * PANEL_W] = p.bg;
      if (step > 0) for (int i = iLo; i < iHi; i++) d[i * PANEL_W] = s[i];
      else          for (int i = iLo; i < iHi; i++) d[i * PANEL_W] = s[-i];
      for (int i = iHi; i < STRIP_ROWS; i++) d[i * PANEL_W] = p.bg;
    }
    return;
  }
  for (int ly = 0; ly < SCREEN_H; ly++) {
    int px = flipped ? ly : (PANEL_W - 1 - ly);
    uint16_t* d = dst + px;
    for (int i = 0; i < STRIP_ROWS; i++) {
      int lx = flipped ? (SCREEN_W - 1 - (py0 + i)) : (py0 + i);
      d[i * PANEL_W] = srcPixel(p, lx, ly);
    }
  }
}

static void presentSrc(const PresentSrc& p) {
  if (!panelHandle || !p.a) return;
  uint32_t t0 = micros();

  // Build the first strip before waiting on TE, so the transfer can start the
  // moment the edge arrives.
  fillStrip(strips[0], p, 0);

  if (invertWanted != invertApplied) {
    esp_lcd_panel_invert_color(panelHandle, invertWanted);
    invertApplied = invertWanted;
  }

  uint32_t tw = micros();
  xSemaphoreTake(teSem, 0);                          // drop a stale edge
  xSemaphoreTake(teSem, pdMS_TO_TICKS(TE_WAIT_MS));  // start on a fresh one
  lastTeWaitUs = micros() - tw;

  for (int s = 0; s < STRIP_COUNT; s++) {
    uint16_t* buf = strips[s & 1];
    // draw_bitmap blocks in its CASET param write until the previous strip's
    // DMA has drained, so by the time strip s+1 is filled into the other
    // buffer, strip s-1 (which used it) is guaranteed done. No extra fences.
    esp_lcd_panel_draw_bitmap(panelHandle, 0, s * STRIP_ROWS, PANEL_W, (s + 1) * STRIP_ROWS, buf);
    if (s + 1 < STRIP_COUNT) fillStrip(strips[(s + 1) & 1], p, (s + 1) * STRIP_ROWS);
  }
  lastPresentUs = micros() - t0;
  presentCount++;
}

static inline uint16_t swap16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }

void displayPresent(const uint16_t* frame, int shiftX, int shiftY, uint16_t bg) {
  PresentSrc p = {frame, nullptr, 0, true, shiftX, shiftY, swap16(bg), borderThickness, swap16(borderColor)};
  presentSrc(p);
}

void displayPresentSlide(const uint16_t* from, const uint16_t* to, int offset, bool forward,
                         int shiftX, int shiftY, uint16_t bg) {
  if (offset < 0) offset = 0;
  if (offset > SCREEN_W) offset = SCREEN_W;
  PresentSrc p = {from, to, offset, forward, shiftX, shiftY, swap16(bg), 0, 0};
  presentSrc(p);
}

uint32_t displayLastTeWaitUs() { return lastTeWaitUs; }
