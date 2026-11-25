// sim/layout.js -- screen constants and shared boxes (src/state.h CONSTANTS + LAYOUT).

// ── CONSTANTS (mirroring src/state.h / tokens.h / main.cpp) ──
// PARITY: every number in sim/*.js that places pixels is a copy of the
// firmware's. Change the firmware, change it here too (CLAUDE.md). Design values
// are design.md's tokens -- sim/tokens.js mirrors src/tokens.h line for line.
const SCREEN_W = 480, SCREEN_H = 320;
const POLL_DEFAULT_MS = 20000;
const PAGE_COUNT = 7;
// Pages (0-indexed): 0 Status, 1 Usage (limits + projects + trend), 2 Cats,
// 3 Movies, 4 Status + cats, 5 Note, 6 Overview.
const GIF_PAGE = 2;    // cat GIFs on the device (placeholder here -- see drawGifPage)
const MOVIE_PAGE = 3;  // movies on the device (placeholder here -- see drawGifPage)
const MIXED_PAGE = 4;  // limits column + cats pane
const NOTE_PAGE = 5;   // limits column + note pane (an ordinary render() page)
const OVERVIEW_PAGE = 6;  // the cards on a 3x3 grid, one cell the cats (a media page, like MIXED)
const OFFLINE_AFTER_MS = 180000;  // wall-clock floor, like main.cpp -- never a poll-cycle count
const BOOT_PAGE_AUTO = -1;

// ── LAYOUT (state.h's LAYOUT block) ──────────────────────────
const SWIPE_SPLIT_X = TOK_LAYOUT_HALF_SPLIT_X;
const LIMIT_CARD_H = 104, BTC_CARD_H = 60;
const LIMIT5H_Y = TOK_LAYOUT_CONTENT_Y0;
const LIMITWK_Y = LIMIT5H_Y + LIMIT_CARD_H + TOK_SPACE_GUTTER;
const BTC_Y = LIMITWK_Y + LIMIT_CARD_H + TOK_SPACE_GUTTER;
const CLOCK_CARD_H = 192, WEATHER_CARD_H = 84;
const CLOCK_CARD_Y = TOK_LAYOUT_CONTENT_Y0;
const WEATHER_CARD_Y = CLOCK_CARD_Y + CLOCK_CARD_H + TOK_SPACE_GUTTER;
const WEATHER_HIT_X0 = TOK_LAYOUT_COL_RIGHT_X, WEATHER_HIT_X1 = TOK_LAYOUT_CONTENT_X1;
const WEATHER_HIT_Y0 = WEATHER_CARD_Y, WEATHER_HIT_Y1 = TOK_LAYOUT_CONTENT_Y1;
const STRIP_HIT_Y0 = SCREEN_H - TOK_TOUCH_EDGE_MIN;  // 280: 56x40 strip targets (touch.edge.min)
const HEALTH_HIT_X0 = 0, HEALTH_HIT_X1 = 56;
const SETTINGS_HIT_X0 = 424, SETTINGS_HIT_X1 = SCREEN_W;
const STRIP_CY = 309;
const STRIP_CAPTION_Y = 301;
const SHUFFLE_DISC_R = 16, SHUFFLE_HIT = 48;
const SLEEP_HIT_X0 = TOK_CORNER_HIT_X0, SLEEP_HIT_X1 = SCREEN_W, SLEEP_HIT_Y0 = 0, SLEEP_HIT_Y1 = TOK_CORNER_HIT_Y1;
const MIXED_GIF_X0 = TOK_LAYOUT_COL_RIGHT_X, MIXED_GIF_W = TOK_LAYOUT_COL_RIGHT_W;
const MIXED_GIF_Y0 = TOK_LAYOUT_CONTENT_Y0, MIXED_GIF_H = TOK_LAYOUT_CONTENT_Y1 - TOK_LAYOUT_CONTENT_Y0;
// Overview grid (design.md 7.5): 149 + 8 + 150 + 8 + 149 = 464, 89 + 8 + 90 + 8 + 89 = 284.
const OV_COL_X = [8, 165, 323], OV_COL_W = [149, 150, 149];
const OV_ROW_Y = [8, 105, 203], OV_ROW_H = [89, 90, 89];
// Row by row: 5H, WK, clock / date, month calendar, BTC / weather, cat, note.
const OV_CAT_X0 = 165, OV_CAT_Y0 = 203, OV_CAT_W = 150, OV_CAT_H = 89;
const OV_WX_HIT_X0 = 8, OV_WX_HIT_X1 = 157, OV_WX_HIT_Y0 = 203, OV_WX_HIT_Y1 = 292;
const NIGHT_MODE_DIM_VALUE = 64;

const SHIFT_ORBIT = [[0,0],[1,0],[2,0],[2,-1],[2,-2],[1,-2],[0,-2],[0,-1]];
let SHIFT_STEP_MS = 180000;  // firmware default; flash "pixel_shift_min" * 60000
