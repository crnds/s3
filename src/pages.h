// Pages and render (pages.cpp and the per-page files).
#pragma once
#include <Arduino.h>
#include "fonts.h"

// ── PAGES / RENDER (pages.cpp) ─────────────────────────────
// render() composes the current screen (a page, or the Weather / Device
// Stats sheet) into `frame` and presents it. Settings has its own
// renderSettings().
void render();
// The media pages with a static half around a cat pane: Mixed and Overview,
// online only (offline, every page is the full-screen cat).
bool catPanePage(bool offline);
// The cat's box on a catPanePage().
void catPaneRect(int& x, int& y, int& w, int& h);
// A catPanePage()'s static half: everything but the pane, plus the strip.
void drawMediaPageStatic();
// System corner glyphs, drawn LAST on every screen (design.md 11.21).
// overMedia puts each occupied slot on a color.plate backing.
void drawSystemCorner(bool overMedia);
// Shared icon/component primitives (pages.cpp; settings.cpp and gif_player.cpp reuse these).
void drawCloseGlyph(int cx, int cy, uint16_t c);
void drawBackGlyph(int cx, int cy, uint16_t c);
void drawChevron(int cx, int cy, uint16_t c);
void drawShuffleButton(int cx, int cy, bool pressed);
void drawModalHeader(bool back, const char* title, bool pressed);
void drawCardSurface(int x, int y, int w, int h, uint16_t fill);
// Anti-aliased fills (design.md 8.1): LovyanGFX's fillSmoothRoundRect /
// fillSmoothCircle algorithm, blended straight into the frame buffer.
void aaFillRoundRect(int x, int y, int w, int h, int r, uint16_t c);
void aaFillCircle(int x, int y, int r, uint16_t c);
void aaRing(int cx, int cy, int r, int t, uint16_t c);  // AA ring, outer radius r, thickness t
void aaMaskRoundCorners(int x, int y, int w, int h, int r, uint16_t bg);  // round the corners of media in the frame
// The full-screen cat page's reset readout, on a color.plate (design.md 11.20).
void drawResetPlate();
// Empty / error state (design.md 13.4), centred in a box: a title, space.sm,
// a caption description. isError puts the title in status.error.
void drawEmptyState(int cx, int y0, int h, const char* title, const char* desc, bool isError,
                    FontId titleFont = TOK_TYPE_HEADLINE, uint16_t titleColor = TOK_COLOR_TEXT_PRIMARY);
// The shuffle button's centre for the current media layout (full screen or
// the mixed pane).
void shuffleCentre(bool offline, int& cx, int& cy);
// The two usage windows, and the pace projection: seconds until the limit at
// the window's average rate so far, or -1 when unknown or the reset lands
// first. Shared by the pace flag and networkTask's sound alerts.
// SESSION_WINDOW_SEC, WEEK_WINDOW_SEC and paceSecondsToLimit() live in pace.h.
// Stale data (design.md 13.4): the dashboard is up, but the last good poll is
// older than max(2 poll intervals, 1 min), or there has been none since boot
// (the SD cache). The strip says how old; the limit numerals go secondary.
bool dataStale();

// Per-page / per-sheet draw entry points (render() and nav.cpp dispatch on them),
// one per file: page_status, page_usage, page_note, page_overview, sheet_device,
// sheet_weather, strip.
void drawStatusPage();
void drawUsagePage();
void drawNotePage();
void drawNoteText(int tx, int ty, int widthPx, int tyMax, FontId font);
void drawOverviewCells();
void drawDevicePage();
void drawWeatherPage();
void drawStatusStrip();
