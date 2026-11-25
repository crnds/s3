// Navigation (nav.cpp).
#pragma once
#include <Arduino.h>

// ── NAVIGATION (nav.cpp) ───────────────────────────────────
// The touch router, gesture recogniser and every transition (page slide,
// lean, swipe, sheet rise/drop with scrim, Settings push/pop, reduced-motion
// cross-fade). design.md sections 9 and 12.
enum PressId {
  PRESS_NONE = 0, PRESS_SLEEP, PRESS_CLOSE, PRESS_WEATHER, PRESS_HEALTH, PRESS_GEAR,
  PRESS_SHUFFLE, PRESS_ROW, PRESS_CELL
};
extern PressId pressedId;
extern int pressedIndex;          // row / cell index for PRESS_ROW / PRESS_CELL
void navTouch(bool down, int32_t x, int32_t y, uint32_t now);
// Steps the running spring / momentum and presents. Returns true if it
// presented this pass (loop() then skips its own present).
bool navTick(uint32_t now);
bool navTransitionActive();       // a composite (slide / sheet / fade) is on screen
bool navPageTransitionActive();   // a page slide (or Reduce Motion page fade): media players hold
void navInvalidatePosters();      // theme changed: the media pages' slide-in posters are stale
bool navSheetOpen();              // Weather, Device Stats or Settings is up
void navGoToPage(int page, bool forward);   // serial keys; animated unless offline
void navOpenSheet(int which);     // 0 weather, 1 device, 2 settings
void navRefreshBehind();          // theme changed under an open sheet: recompose the page behind it
void navCloseSheet();
void navFinishTransition();       // snap any running transition to its end
bool navCatLayout();              // the cat player owns the current page
void navSyncCatMode(bool catMode);  // allocate / free the GIF decoder on catMode edges
void navResetGesture();           // drop any gesture in progress (screen sleep)
// Screen sleep (main.cpp): backlight fade to 0, panel DISPOFF + SLPIN, CPU
// 80MHz, WiFi modem sleep; the next press anywhere wakes (and is swallowed).
void enterScreenSleep();
void exitScreenSleep();
