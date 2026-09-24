// AXS15231B touch read, ported from the touch half of the seller's
// esp_lcd_axs15231b.c (dropped from the vendored lib/ copy) onto Arduino Wire.
#include "touch_axs.h"
#include "display.h"
#include "pins.h"

#include <Arduino.h>
#include <Wire.h>

// "Read touch report" command: magic b5 ab a5 5a, then the report length
// (8 bytes = 2-byte header + one 6-byte point) big-endian in bytes 6-7.
static const uint8_t READ_CMD[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00};
static const int REPORT_LEN = 8;
static bool touchOk = false;

bool touchBegin() {
  touchOk = Wire.begin(S3_TOUCH_SDA, S3_TOUCH_SCL, 400000);
  pinMode(S3_TOUCH_INT, INPUT_PULLUP);
  return touchOk;
}

bool touchRead(int32_t& x, int32_t& y) {
  if (!touchOk) return false;
  Wire.beginTransmission(S3_TOUCH_ADDR);
  Wire.write(READ_CMD, sizeof(READ_CMD));
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom((uint8_t)S3_TOUCH_ADDR, (uint8_t)REPORT_LEN) != REPORT_LEN) return false;
  uint8_t d[REPORT_LEN];
  for (int i = 0; i < REPORT_LEN; i++) d[i] = Wire.read();

  // d[0] gesture (ignored), d[1] point count, d[2..5] point 0:
  // x = (d[2] & 0x0F) << 8 | d[3], y = (d[4] & 0x0F) << 8 | d[5], portrait.
  uint8_t num = d[1];
  if (num != 1) return false;
  int px = ((d[2] & 0x0F) << 8) | d[3];
  int py = ((d[4] & 0x0F) << 8) | d[5];
  if (px >= 320 || py >= 480) return false;  // garbage frame, not a touch

  // Inverse of display.cpp's fillStrip() mapping:
  //   normal:  lx = py,       ly = 319 - px
  //   flipped: lx = 479 - py, ly = px
  if (!displayFlipped()) {
    x = py;
    y = 319 - px;
  } else {
    x = 479 - py;
    y = px;
  }
  return true;
}
