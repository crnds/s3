#pragma once

#include <stdint.h>

// Capacitive touch on the AXS15231B (I2C 0x3B). Single-touch, like the
// seller's driver: a second finger makes the chip report 2 points, which is
// treated as "no touch" (same as esp_lcd_axs15231b.c's AXS_MAX_TOUCH_NUMBER=1).
bool touchBegin();
// Landscape screen coordinates (0..479, 0..319) in the current rotation
// (display.h's displayFlipped()). Returns false when no finger is down.
bool touchRead(int32_t& x, int32_t& y);
