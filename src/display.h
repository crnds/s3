#pragma once

#include <stdint.h>

// Panel side of the graphics pipeline (display.cpp). LovyanGFX is used only as
// an off-screen canvas (a 480x320 LGFX_Sprite in PSRAM); this module owns the
// real AXS15231B panel and gets that canvas onto it.
//
// The panel is natively 320x480 portrait and, over QSPI, only accepts whole
// frames streamed from row 0 (see lib/axs15231b's header comment), so there is
// exactly one way to update it: displayPresent() rotates the full landscape
// frame into portrait strips and streams all of them, starting on a TE edge.

const int SCREEN_W = 480;   // landscape, what every page draws in
const int SCREEN_H = 320;

bool displayBegin();
// 0-255, LEDC PWM on the backlight MOSFET.
void displaySetBrightness(uint8_t level);
// Screen sleep: true = DISPOFF + SLPIN (panel GRAM is kept), false = SLPOUT
// + DISPON. Blocks ~120ms on wake. The backlight is the caller's job.
void displaySetSleep(bool sleep);
// false = 90deg software rotation (the seller demo's default orientation),
// true = 270deg (the Settings "FLIPPED" option). Also read by touch_axs.cpp.
void displaySetFlipped(bool flipped);
bool displayFlipped();

// Push a full landscape 480x320 RGB565 frame (LovyanGFX sprite byte order,
// i.e. big-endian, which is what the panel expects). shiftX/shiftY is the
// anti-retention pixel-shift offset: the image is drawn displaced by it and
// the uncovered margin is filled with `bg`. Returns once the last strip is
// queued (DMA may still be draining it); the caller may redraw the canvas
// immediately, the strips are separate internal-RAM buffers.
void displayPresent(const uint16_t* frame, int shiftX, int shiftY, uint16_t bg);

// Page-change slide: composite two full frames in the rotate copy (no third
// buffer). `offset` is how far (0..SCREEN_W) the incoming frame has slid in;
// forward = the new page enters from the right, backward = from the left.
void displayPresentSlide(const uint16_t* from, const uint16_t* to, int offset, bool forward,
                         int shiftX, int shiftY, uint16_t bg);
// Sheet rise/drop (design.md 12.5): `sheet` occupies the bottom `visibleH`
// rows (its top row at y = 320 - visibleH; past 320 = overshoot, bg below it),
// the page `behind` shows above it through a scrim step (0 none, 1 = 75%,
// 2 = 50% -- design.md 8.4). Same cost as the horizontal slide: the offset and
// the scrim are applied in the rotate copy that runs anyway.
void displayPresentSheet(const uint16_t* behind, const uint16_t* sheet, int visibleH, int scrimLevel,
                         int shiftX, int shiftY, uint16_t bg);
// Reduce Motion cross-fade: level 1..3 = 25 / 50 / 75% of `to` over `from`.
void displayPresentFade(const uint16_t* from, const uint16_t* to, int level,
                        int shiftX, int shiftY, uint16_t bg);

// Diagnostics for the serial log / Device Stats.
uint32_t displayTeCount();          // TE edges seen since boot
uint32_t displayLastPresentUs();    // wall time of the most recent displayPresent()
uint32_t displayPresentCount();
uint32_t displayLastTeWaitUs();     // part of the last present spent idle waiting for TE
