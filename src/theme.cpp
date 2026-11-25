// Theme and contrast: applyThemeTokens / applyContrast assign the runtime TOK_COLOR_* roles
// (design.md 4.6, 12.9). Twin: sim/theme.js. Split from settings.cpp (roadmap O10).
#include "state.h"

// Increase Contrast (design.md 12.9): text.secondary and fill.track step
// further from the surface, and cards gain a 1px gray.4 outline. Meaning
// unchanged, separation up. Theme-aware, so applyThemeTokens() ends with it.
void applyContrast() {
  if (cfgLightMode) {
    TOK_COLOR_TEXT_SECONDARY = cfgHighContrast ? TOK_LIGHT_GRAY_TEXT_2_HC : TOK_LIGHT_GRAY_TEXT_2;
    TOK_COLOR_FILL_TRACK = cfgHighContrast ? TOK_LIGHT_GRAY_TRACK_HC : TOK_LIGHT_GRAY_PRESSED;
  } else {
    TOK_COLOR_TEXT_SECONDARY = cfgHighContrast ? TOK_GRAY_5_HC : TOK_GRAY_5;
    TOK_COLOR_FILL_TRACK = cfgHighContrast ? TOK_GRAY_4 : TOK_GRAY_3;
  }
  TOK_CARD_OUTLINE = cfgHighContrast;
}

// Dark / Light theme (design.md 4.6): assigns every theme role of tokens.h from
// the active palette. Colours are read at draw time, so the next render()
// shows the swap; buffers that already hold pixels (nav.cpp's behindFrame /
// prevFrame) are the caller's to refresh.
void applyThemeTokens() {
  if (cfgLightMode) {
    TOK_COLOR_BG_CANVAS = TOK_LIGHT_GRAY_CANVAS;
    TOK_COLOR_SURFACE_CARD = TOK_LIGHT_GRAY_CARD;
    TOK_COLOR_SURFACE_RAISED = TOK_LIGHT_GRAY_RAISED;
    TOK_COLOR_FILL_PRESSED = TOK_LIGHT_GRAY_PRESSED;
    TOK_COLOR_SEPARATOR = TOK_LIGHT_GRAY_PRESSED;
    TOK_COLOR_TEXT_PRIMARY = TOK_GRAY_0;
    TOK_COLOR_ACCENT = TOK_COLOR_DATA_USAGE = TOK_LIGHT_CORAL;
    TOK_COLOR_ACCENT_PRESSED = TOK_LIGHT_CORAL_PRESSED;
    TOK_COLOR_STATUS_SUCCESS = TOK_LIGHT_GREEN;
    TOK_COLOR_STATUS_WARNING = TOK_LIGHT_AMBER;
    TOK_COLOR_DATA_PACE = TOK_LIGHT_GREEN;
    TOK_COLOR_DATA_PACE_WEDGE = TOK_LIGHT_GREEN_WEDGE;
    TOK_COLOR_DATA_PACE_SHINE_LO = TOK_LIGHT_GREEN_SHINE_LO;
    TOK_COLOR_DATA_PACE_SHINE_MID = TOK_LIGHT_GREEN_SHINE_MID;
    TOK_COLOR_DATA_PACE_SHINE_HI = TOK_LIGHT_GREEN_SHINE_HI;
    TOK_COLOR_CONTENT_SUN = TOK_LIGHT_AMBER;
    TOK_COLOR_CONTENT_SNOW = TOK_GRAY_4;
    TOK_COLOR_CONTENT_CLOUD = TOK_LIGHT_GRAY_CLOUD;
  } else {
    TOK_COLOR_BG_CANVAS = TOK_GRAY_0;
    TOK_COLOR_SURFACE_CARD = TOK_GRAY_1;
    TOK_COLOR_SURFACE_RAISED = TOK_GRAY_2;
    TOK_COLOR_FILL_PRESSED = TOK_GRAY_3;
    TOK_COLOR_SEPARATOR = TOK_GRAY_3;
    TOK_COLOR_TEXT_PRIMARY = TOK_GRAY_6;
    TOK_COLOR_ACCENT = TOK_COLOR_DATA_USAGE = TOK_CORAL_500;
    TOK_COLOR_ACCENT_PRESSED = TOK_CORAL_600;
    TOK_COLOR_STATUS_SUCCESS = TOK_GREEN_500;
    TOK_COLOR_STATUS_WARNING = TOK_AMBER_500;
    TOK_COLOR_DATA_PACE = TOK_GREEN_500;
    TOK_COLOR_DATA_PACE_WEDGE = TOK_GREEN_WEDGE;
    TOK_COLOR_DATA_PACE_SHINE_LO = TOK_GREEN_SHINE_LO;
    TOK_COLOR_DATA_PACE_SHINE_MID = TOK_GREEN_SHINE_MID;
    TOK_COLOR_DATA_PACE_SHINE_HI = TOK_GREEN_SHINE_HI;
    TOK_COLOR_CONTENT_SUN = TOK_SUN_500;
    TOK_COLOR_CONTENT_SNOW = TOK_GRAY_6;
    TOK_COLOR_CONTENT_CLOUD = TOK_GRAY_5;
  }
  applyContrast();
}
