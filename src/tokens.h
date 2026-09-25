// Design tokens -- design.md is the source of truth; simulator-s3.html
// mirrors every line (its TOKENS block, RGB888 with the `// 0x....` beside
// each). Naming is `TOK_` + the upper snake of the dotted token name
// (design.md section 3.2). Screens use only the SEMANTIC tokens below the
// primitives; a raw colour literal, font id or magic spacing number in draw
// code is a defect (design.md section 16).
#pragma once
#include <stdint.h>
#include "fonts.h"

// ── COLOUR: PRIMITIVES (design.md 4.1) ─────────────────────
// Exact RGB565 grays (R = B, G = 2R) plus the hue set. Screens never use
// these directly -- only the semantic roles further down.
const uint16_t TOK_GRAY_0 = 0x0841;   // rgb(8,8,8)
const uint16_t TOK_GRAY_1 = 0x18C3;   // rgb(25,24,25)
const uint16_t TOK_GRAY_2 = 0x2945;   // rgb(41,40,41)
const uint16_t TOK_GRAY_3 = 0x39C7;   // rgb(58,57,58)
const uint16_t TOK_GRAY_4 = 0x6B4D;   // rgb(107,105,107)
const uint16_t TOK_GRAY_5 = 0x9CD3;   // rgb(156,154,156)
const uint16_t TOK_GRAY_6 = 0xFFFF;   // rgb(255,255,255)
const uint16_t TOK_GRAY_5_HC = 0xBDF7;  // rgb(189,190,189) -- Increase Contrast's text.secondary
const uint16_t TOK_CORAL_500 = 0xFB08;  // rgb(255,97,66)
const uint16_t TOK_CORAL_600 = 0xCA66;  // rgb(206,77,49)
const uint16_t TOK_GREEN_500 = 0x2668;  // rgb(33,206,66)
const uint16_t TOK_GREEN_WEDGE = 0x1B65;     // rgb(25,109,41) -- green.500 at 50% over gray.1
const uint16_t TOK_GREEN_SHINE_LO = 0x5ECE;  // rgb(90,219,115)
const uint16_t TOK_GREEN_SHINE_MID = 0x9734; // rgb(148,231,165)
const uint16_t TOK_GREEN_SHINE_HI = 0xD7BA;  // rgb(214,247,214)
const uint16_t TOK_AMBER_500 = 0xFD20;  // rgb(255,166,0)
const uint16_t TOK_RED_500 = 0xF8C6;    // rgb(255,24,49)
const uint16_t TOK_BLUE_500 = 0x3C1E;   // rgb(58,130,247)
const uint16_t TOK_SUN_500 = 0xFE60;    // rgb(255,206,0)
const uint16_t TOK_BLACK = 0x0000;      // rgb(0,0,0)

// External scale (exempt from the palette): US EPA / aqicn.org AQI.
const uint16_t TOK_AQI_GOOD = TOK_GREEN_500;
const uint16_t TOK_AQI_MODERATE = 0xFFE0;   // rgb(255,255,0)
const uint16_t TOK_AQI_USG = 0xFB82;        // rgb(255,113,16)
const uint16_t TOK_AQI_UNHEALTHY = TOK_RED_500;
const uint16_t TOK_AQI_VERY_UNHEALTHY = 0xAABE;  // rgb(173,85,247)
const uint16_t TOK_AQI_HAZARDOUS = 0x78E3;       // rgb(123,28,25)

// ── COLOUR: SEMANTIC (design.md 4.2) ───────────────────────
const uint16_t TOK_COLOR_BG_CANVAS = TOK_GRAY_0;
const uint16_t TOK_COLOR_SURFACE_CARD = TOK_GRAY_1;
const uint16_t TOK_COLOR_SURFACE_RAISED = TOK_GRAY_2;
const uint16_t TOK_COLOR_FILL_PRESSED = TOK_GRAY_3;
const uint16_t TOK_COLOR_SEPARATOR = TOK_GRAY_3;
const uint16_t TOK_COLOR_TEXT_PRIMARY = TOK_GRAY_6;
const uint16_t TOK_COLOR_TEXT_TERTIARY = TOK_GRAY_4;
const uint16_t TOK_COLOR_TEXT_ON_ACCENT = TOK_GRAY_0;
const uint16_t TOK_COLOR_ACCENT = TOK_CORAL_500;
const uint16_t TOK_COLOR_ACCENT_PRESSED = TOK_CORAL_600;
const uint16_t TOK_COLOR_STATUS_SUCCESS = TOK_GREEN_500;
const uint16_t TOK_COLOR_STATUS_WARNING = TOK_AMBER_500;
const uint16_t TOK_COLOR_STATUS_ERROR = TOK_RED_500;
const uint16_t TOK_COLOR_STATUS_INFO = TOK_BLUE_500;
const uint16_t TOK_COLOR_DATA_USAGE = TOK_CORAL_500;
const uint16_t TOK_COLOR_DATA_PACE = TOK_GREEN_500;
const uint16_t TOK_COLOR_DATA_PACE_WEDGE = TOK_GREEN_WEDGE;
const uint16_t TOK_COLOR_DATA_SYSTEM = TOK_BLUE_500;
const uint16_t TOK_COLOR_CONTENT_SUN = TOK_SUN_500;
const uint16_t TOK_COLOR_CONTENT_RAIN = TOK_BLUE_500;
const uint16_t TOK_COLOR_CONTENT_SNOW = TOK_GRAY_6;
const uint16_t TOK_COLOR_CONTENT_CLOUD = TOK_GRAY_5;
const uint16_t TOK_COLOR_PLATE = TOK_BLACK;
// Increase Contrast (design.md 12.9) swaps these at runtime: text.secondary
// -> 0xBDF7, fill.track -> gray.4, and cards gain a 1px gray.4 outline
// (TOK_CARD_OUTLINE). Set by applyContrast() in settings.cpp.
extern uint16_t TOK_COLOR_TEXT_SECONDARY;   // gray.5 (0xBDF7 under Increase Contrast)
extern uint16_t TOK_COLOR_FILL_TRACK;       // gray.3 (gray.4 under Increase Contrast)
extern bool TOK_CARD_OUTLINE;
const uint16_t TOK_COLOR_CARD_OUTLINE = TOK_GRAY_4;

// Note-pane tokenizer colours (design.md 11.19). The rules are the six-copy
// parity surface in CLAUDE.md; only the colours are tokens.
const uint16_t TOK_NOTE_CODE = TOK_COLOR_STATUS_INFO;
const uint16_t TOK_NOTE_KEYWORD_BAD = TOK_COLOR_STATUS_ERROR;
const uint16_t TOK_NOTE_KEYWORD_GOOD = TOK_COLOR_STATUS_SUCCESS;
const uint16_t TOK_NOTE_NUMBER = TOK_COLOR_CONTENT_SUN;
const uint16_t TOK_NOTE_HEADING = TOK_COLOR_ACCENT;
const uint16_t TOK_NOTE_MARKER = TOK_COLOR_ACCENT;
const uint16_t TOK_NOTE_TEXT = TOK_COLOR_TEXT_PRIMARY;
// note.quote is text.secondary, a runtime token -- use TOK_COLOR_TEXT_SECONDARY.

// ── TYPE (design.md 5.2) ───────────────────────────────────
const FontId TOK_TYPE_DISPLAY = FONT_XL;    // 40/650, tracking -1
const FontId TOK_TYPE_TITLE = FONT_LG;      // 26/650, tracking -1
const FontId TOK_TYPE_HEADLINE = FONT_MDB;  // 17/650
const FontId TOK_TYPE_BODY = FONT_MD;       // 17/500
const FontId TOK_TYPE_LABEL = FONT_SMB;     // 13/650, tracking +1, UPPERCASE only
const FontId TOK_TYPE_CAPTION = FONT_SM;    // 13/500
const FontId TOK_TYPE_MONO_S = FONT_MONO1;
const FontId TOK_TYPE_MONO_M = FONT_MONO2;
const FontId TOK_TYPE_MONO_L = FONT_MONO3;
const FontId TOK_TYPE_NUMERAL_HERO = TOK_TYPE_DISPLAY;
const FontId TOK_TYPE_NUMERAL_LG = TOK_TYPE_TITLE;
const FontId TOK_TYPE_NUMERAL_MD = TOK_TYPE_HEADLINE;
const FontId TOK_TYPE_NUMERAL_SM = TOK_TYPE_CAPTION;

// ── SPACING (design.md 6) ──────────────────────────────────
const int TOK_SPACE_NONE = 0;
const int TOK_SPACE_HAIR = 2;
const int TOK_SPACE_XS = 4;
const int TOK_SPACE_SM = 8;
const int TOK_SPACE_MD = 12;
const int TOK_SPACE_LG = 16;
const int TOK_SPACE_XL = 24;
const int TOK_SPACE_XXL = 32;
const int TOK_SPACE_SCREEN_MARGIN = TOK_SPACE_SM;
const int TOK_SPACE_GUTTER = TOK_SPACE_SM;
const int TOK_SPACE_CARD_PAD = TOK_SPACE_MD;
const int TOK_SPACE_CARD_PAD_COMPACT_V = TOK_SPACE_SM;
const int TOK_SPACE_CARD_PAD_COMPACT_H = TOK_SPACE_MD;
const int TOK_SPACE_CARD_PAD_HERO = TOK_SPACE_SM;
const int TOK_SPACE_STACK_TIGHT = TOK_SPACE_XS;
const int TOK_SPACE_STACK = TOK_SPACE_SM;
const int TOK_SPACE_GROUP = TOK_SPACE_LG;
const int TOK_SPACE_LIST_GAP = TOK_SPACE_SM;
const int TOK_SPACE_TOUCH_GAP = TOK_SPACE_SM;
const int TOK_SPACE_ICON_TEXT = TOK_SPACE_SM;
const int TOK_SPACE_ICON_TEXT_INLINE = TOK_SPACE_XS;

// ── LAYOUT (design.md 7) ───────────────────────────────────
const int TOK_LAYOUT_CONTENT_X0 = 8, TOK_LAYOUT_CONTENT_X1 = 472;   // x1 exclusive -> 464 wide
const int TOK_LAYOUT_CONTENT_Y0 = 8, TOK_LAYOUT_CONTENT_Y1 = 280;   // y1 exclusive -> 272 tall
const int TOK_LAYOUT_CONTENT_W = TOK_LAYOUT_CONTENT_X1 - TOK_LAYOUT_CONTENT_X0;
const int TOK_LAYOUT_STRIP_Y0 = 288, TOK_LAYOUT_STRIP_H = 32;       // y 319 is the progress hairline
const int TOK_LAYOUT_OVERLAY_CONTENT_Y1 = 312;                       // sheets: no strip
const int TOK_LAYOUT_HEADER_H = 44;                                  // modal header band y 0..43
const int TOK_LAYOUT_HEADER_CONTENT_Y = 52;                          // first content row under it
const int TOK_LAYOUT_COL_LEFT_X = 8, TOK_LAYOUT_COL_LEFT_W = 172;    // fixed reservation, ends at x 180
const int TOK_LAYOUT_COL_RIGHT_X = 188, TOK_LAYOUT_COL_RIGHT_W = 284;
const int TOK_LAYOUT_HALF_SPLIT_X = 240;                             // page-nav halves: screen halves, not the grid

// System corner (design.md 7.3): glyph boxes + the sleep hit box.
const int TOK_CORNER_SLOT_A_X = 448, TOK_CORNER_SLOT_B_X = 404, TOK_CORNER_SLOT_Y = 8, TOK_CORNER_SLOT_SIZE = 24;
const int TOK_CORNER_HIT_X0 = 436, TOK_CORNER_HIT_Y1 = 44;           // sleep: x 436..479, y 0..43
const int TOK_CORNER_INK_X1 = TOK_CORNER_SLOT_B_X - TOK_SPACE_SM;    // content ink stays left of x 396 ...
const int TOK_CORNER_INK_Y1 = TOK_CORNER_SLOT_Y + TOK_CORNER_SLOT_SIZE + TOK_SPACE_SM;  // ... while above y 40
// Close slot (top-left of every sheet / modal screen).
const int TOK_CLOSE_SLOT_X = 8, TOK_CLOSE_SLOT_Y = 8;
const int TOK_CLOSE_HIT_X1 = 52, TOK_CLOSE_HIT_Y1 = 52;              // x 0..51, y 0..51
const int TOK_HEADER_TITLE_X = 60, TOK_HEADER_TITLE_Y = 11;

// ── SHAPE (design.md 8) ────────────────────────────────────
const int TOK_RADIUS_NONE = 0;
const int TOK_RADIUS_SM = 4;
const int TOK_RADIUS_MD = 8;
// radius.full = h / 2, computed at the call site.

// Stroke: LovyanGFX drawWideLine takes a HALF-width, so these are the `r` arg.
const float TOK_STROKE_GLYPH_R = 1.0f;     // 2px, system glyphs at 16/20px
const float TOK_STROKE_GLYPH_LG_R = 1.5f;  // 3px, system glyphs at 24px

// ── TOUCH (design.md 9) ────────────────────────────────────
const int TOK_TOUCH_MIN = 44;
const int TOK_TOUCH_COMFORTABLE = 56;
const int TOK_TOUCH_EDGE_MIN = 40;
const int TOK_TOUCH_SLOP = 10;
const uint32_t TOK_TOUCH_REARM_MS = 120;
const uint32_t TOK_TOUCH_ARM_WINDOW_MS = 4000;
const uint32_t TOK_TOUCH_WAKE_GUARD_MS = 600;
const uint32_t TOK_TOUCH_VELOCITY_WINDOW_MS = 100;
const float TOK_TOUCH_DECEL = 0.995f;          // per ms
const float TOK_TOUCH_FLICK_COMMIT = 150.0f;   // px/s
const float TOK_TOUCH_FLICK_BOUNCE = 300.0f;   // px/s
const float TOK_TOUCH_RUBBERBAND = 0.55f;

// ── COMPONENT SIZES ────────────────────────────────────────
const int TOK_METER_SM = 6, TOK_METER_MD = 8, TOK_METER_LG = 12;
const int TOK_ICON_SM = 16, TOK_ICON_MD = 20, TOK_ICON_LG = 24;
const int TOK_BADGE_H = 24, TOK_BADGE_PAD_X = 6;
const int TOK_BUTTON_MD = 44, TOK_BUTTON_LG = 56;
const int TOK_OPTION_CELL_H = 64;
const int TOK_LIST_ROW_H = TOK_TOUCH_COMFORTABLE;
const int TOK_LIST_ROW_INSET = TOK_SPACE_LG;
const int TOK_TOGGLE_W = 52, TOK_TOGGLE_H = 28;
const int TOK_TOAST_H = 36;
const int TOK_PRESSED_DISC_R = 20;   // icon button pressed disc (40px)
const int TOK_DISCLOSURE_COL = 24;   // a tappable card reserves this for its chevron

// ── MOTION (design.md 12) ──────────────────────────────────
// Springs: damping ratio x100, response in ms.
const uint16_t TOK_MOTION_SPRING_STANDARD_ZETA = 100, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS = 250;
const uint16_t TOK_MOTION_SPRING_SHEET_ZETA = 100, TOK_MOTION_SPRING_SHEET_RESPONSE_MS = 300;
const uint16_t TOK_MOTION_SPRING_FLICK_ZETA = 80, TOK_MOTION_SPRING_FLICK_RESPONSE_MS = 300;
const int TOK_MOTION_LEAN_PX = 8;
const uint32_t TOK_MOTION_SWEEP_MS = 600;
const int TOK_MOTION_PULSE_FRAMES = 3;
const int TOK_MOTION_FADE_FRAMES = 3;
// motion.progress.maxHz: not a fixed rate -- the hairline re-presents as soon
// as it would grow by one physical pixel (POLL_INTERVAL_MS/SCREEN_W), floored
// here at ~30 Hz (the loop's own throughput ceiling, design.md 12.7) so a
// short poll interval can't out-pace the render loop.
const uint32_t TOK_MOTION_PROGRESS_FLOOR_MS = 33;
const uint32_t TOK_MOTION_TOAST_MS = 2500;
const uint32_t TOK_MOTION_BACKLIGHT_SLEEP_MS = 200;
const uint32_t TOK_MOTION_BACKLIGHT_WAKE_MS = 200;
const uint32_t TOK_MOTION_BACKLIGHT_NIGHT_MS = 2000;
const uint32_t TOK_MOTION_BACKLIGHT_ADJUST_MS = 150;
const uint32_t TOK_MOTION_BACKLIGHT_BREATH_MS = 1000;  // one dip-and-back; three of them
const int TOK_MOTION_BACKLIGHT_BREATH_PCT = 40;
const int TOK_MOTION_BACKLIGHT_BREATH_COUNT = 3;
const int TOK_MOTION_FRAME_MAX_PX = 150;
