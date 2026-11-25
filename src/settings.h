// Settings sheet (settings.cpp).
#pragma once
#include <Arduino.h>

// ── SETTINGS (settings.cpp) ────────────────────────────────
// The Settings sheet: SET_LIST (scrolling list of rows) and SET_LEAF (a
// detail screen with an option grid or an arm button). nav.cpp owns the
// gestures and transitions; these are the drawing, hit-testing and commit
// halves.
void renderSettings();          // compose + present
void drawSettingsScreen();      // compose only (transitions snapshot around it)
void queueConfigSave(uint8_t keyId, int32_t value);
// Recompute live backlight (night mode) / poll cadence (battery save). Call
// after any of those inputs change (and once after loadRuntimeConfig).
// fadeMs picks the backlight motion token (0 = cut).
void applyEffectiveBrightness(uint32_t fadeMs = 0);
void applyEffectivePoll();
void applyContrast();
void applyThemeTokens();
// Reads only the theme key from NVS so the very first frame is painted in the
// right theme (setup(), before loadRuntimeConfig()).
void loadThemeEarly();
int settingsListHit(int32_t x, int32_t y);        // row index, or -1
// Tap on a list row (commit on up): toggles and buttons act in place and
// return SG_NONE; navigation/action rows set settingsLeafIndex and return
// SG_LEAF (push the detail); a section row returns SG_SECTION (push the child
// list: nav.cpp snapshots the screen, then calls settingsEnterSection()).
enum SettingsGo { SG_NONE, SG_LEAF, SG_SECTION };
SettingsGo settingsListActivate(int idx);
// -1 = the main list; else the SETTINGS row whose children the list shows.
extern int settingsSection;
void settingsEnterSection();
void settingsLeaveSection();
void settingsUndoLeaveSection();
int settingsLeafHit(int32_t x, int32_t y);        // option cell / arm button, or -1
void settingsLeafActivate(int idx, uint32_t now);
int settingsScrollMax();
// Toast (design.md 11.17): confirms an action with no other visible result.
extern char toastText[40];
extern uint32_t toastUntilMs;
