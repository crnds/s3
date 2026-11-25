// Screen layout boxes shared by pages, nav, settings and the media players
// (design.md section 7). simulator layout.js carries the same numbers.
#pragma once
#include <Arduino.h>
#include "tokens.h"

// ── LAYOUT (480x320 landscape) ──────────────────────────────
// design.md section 7 is the source of truth; the numbers here are the
// shared hit boxes and card boxes the touch router (nav.cpp), pages.cpp,
// settings.cpp and gif_player.cpp all need. simulator-s3.html carries the
// same numbers. Colours, type, spacing and component sizes are tokens
// (tokens.h) -- no COL_* literals any more.
//   - 8px screen margin and card gutter; content x 8..471, y 8..291
//   - left column x 8..135 (fixed 128px), right column x 144..471
//   - status strip y 300..318, progress hairline y 319
// Page navigation splits at the screen half, independent of the grid.
const int SWIPE_SPLIT_X = TOK_LAYOUT_HALF_SPLIT_X;

// Status page cards (design.md 7.4).
const int LIMIT_CARD_H = 104, BTC_CARD_H = 60;
const int LIMIT5H_Y = TOK_LAYOUT_CONTENT_Y0;                              // 8
const int LIMITWK_Y = LIMIT5H_Y + LIMIT_CARD_H + TOK_SPACE_GUTTER;       // 120
const int BTC_Y = LIMITWK_Y + LIMIT_CARD_H + TOK_SPACE_GUTTER;           // 232
const int CLOCK_CARD_H = 192, WEATHER_CARD_H = 84;
const int CLOCK_CARD_Y = TOK_LAYOUT_CONTENT_Y0;                           // 8
const int WEATHER_CARD_Y = CLOCK_CARD_Y + CLOCK_CARD_H + TOK_SPACE_GUTTER;  // 208

// Weather card (status page) = its own hit box: the whole card is the
// target, with a disclosure chevron as its affordance. Tap opens the Weather
// sheet.
const int WEATHER_HIT_X0 = TOK_LAYOUT_COL_RIGHT_X, WEATHER_HIT_X1 = TOK_LAYOUT_CONTENT_X1;
const int WEATHER_HIT_Y0 = WEATHER_CARD_Y, WEATHER_HIT_Y1 = TOK_LAYOUT_CONTENT_Y1;

// Overview page grid (design.md 7.5): 3 x 3 cells in the 464 x 284 content
// area with 8px gutters -- 149 + 8 + 150 + 8 + 149 = 464, 89 + 8 + 90 + 8 + 89
// = 284. Row by row: 5H, WK, clock / date, month calendar, BTC / weather,
// cat, note.
const int OV_COL_X[3] = {8, 165, 323}, OV_COL_W[3] = {149, 150, 149};
const int OV_ROW_Y[3] = {8, 105, 203}, OV_ROW_H[3] = {89, 90, 89};
// The cat cell (bottom-middle, the GIF player's pane on this page) and the
// weather cell (bottom-left), whose whole box opens the Weather sheet -- bar
// its bottom-left corner, which the Device Stats strip target takes first.
const int OV_CAT_X0 = 165, OV_CAT_Y0 = 203, OV_CAT_W = 150, OV_CAT_H = 89;
const int OV_WX_HIT_X0 = 8, OV_WX_HIT_X1 = 157, OV_WX_HIT_Y0 = 203, OV_WX_HIT_Y1 = 292;

// Status strip targets (design.md 11.12): 56x40 each (touch.edge.min for a
// bottom-edge target) -- the 20px strip, the 8px gap and the bottom 12px of the
// content area. nav.cpp's hit order gives them that strip of content ahead of a
// card, but not ahead of the shuffle button (design.md 9.2).
const int STRIP_HIT_Y0 = SCREEN_H - TOK_TOUCH_EDGE_MIN;                   // 280
const int HEALTH_HIT_X0 = 0, HEALTH_HIT_X1 = 56;                          // -> Device Stats
const int SETTINGS_HIT_X0 = 424, SETTINGS_HIT_X1 = SCREEN_W;              // -> Settings
const int STRIP_CY = 309;                                                 // glyph centre line
const int STRIP_CAPTION_Y = 301;                                          // caption text top (design.md 11.12)

// Media control (design.md 11.20): a shuffle icon button, only while Cat
// Shuffle is Fixed, bottom-right of the media area, 8px in from its edges.
// It replaces the old invisible "next cat" zones.
const int SHUFFLE_DISC_R = 16;
const int SHUFFLE_HIT = 48;

// Sleep button (corner slot a) -- checked before every other target.
const int SLEEP_HIT_X0 = TOK_CORNER_HIT_X0, SLEEP_HIT_X1 = SCREEN_W;

// Mixed page's cat pane = the right column (design.md 7.4): the GIF player
// cover-fits the cat into it. Defined here so pages.cpp's placeholder and
// gif_player.cpp agree. The Overview page's pane is its cat cell (OV_CAT_*).
const int MIXED_GIF_X0 = TOK_LAYOUT_COL_RIGHT_X, MIXED_GIF_W = TOK_LAYOUT_COL_RIGHT_W;
const int MIXED_GIF_Y0 = TOK_LAYOUT_CONTENT_Y0, MIXED_GIF_H = TOK_LAYOUT_CONTENT_Y1 - TOK_LAYOUT_CONTENT_Y0;
