// render(), the media pages with a static half, media overlays.
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"

// ── MEDIA PAGES WITH A STATIC HALF (Mixed, Overview) ───────
bool catPanePage(bool offline) {
  return !offline && (currentPage == MIXED_PAGE || currentPage == OVERVIEW_PAGE);
}

void catPaneRect(int& x, int& y, int& w, int& h) {
  if (currentPage == OVERVIEW_PAGE) { x = OV_CAT_X0; y = OV_CAT_Y0; w = OV_CAT_W; h = OV_CAT_H; }
  else { x = MIXED_GIF_X0; y = MIXED_GIF_Y0; w = MIXED_GIF_W; h = MIXED_GIF_H; }
}

// Everything but the cat pane, which belongs to the GIF player
// (gif_player.cpp cover-fits the cat there), plus the strip.
void drawMediaPageStatic() {
  int px, py, pw, ph;
  catPaneRect(px, py, pw, ph);
  g->fillRect(0, 0, SCREEN_W, py, TOK_COLOR_BG_CANVAS);
  g->fillRect(0, py + ph, SCREEN_W, SCREEN_H - py - ph, TOK_COLOR_BG_CANVAS);
  g->fillRect(0, py, px, ph, TOK_COLOR_BG_CANVAS);
  g->fillRect(px + pw, py, SCREEN_W - px - pw, ph, TOK_COLOR_BG_CANVAS);
  if (currentPage == OVERVIEW_PAGE) {
    drawOverviewCells();
  } else {
    drawLimitsColumn();
    drawBtcCard();
  }
  drawStatusStrip();
}


// ── RENDER ─────────────────────────────────────────────────
void render() {
  // The media pages are animated frame-by-frame by gifTick() in loop(), not
  // drawn here (unless a sheet is up over them); offline is also gifTick()'s.
  bool sheet = weatherPageOpen || devicePageOpen;
  if (!sheet && (currentPage == GIF_PAGE || currentPage == MOVIE_PAGE || currentPage == MIXED_PAGE ||
                 currentPage == OVERVIEW_PAGE)) return;
  uint32_t startUs = micros();
  // Hold the lock across all STATE reads, release before the present.
  lockState();
  g->fillScreen(TOK_COLOR_BG_CANVAS);
  if (weatherPageOpen) {
    drawWeatherPage();
  } else if (devicePageOpen) {
    drawDevicePage();
  } else {
    switch (currentPage) {
      case 0: drawStatusPage(); break;
      case 1: drawUsagePage(); break;     // limits column + top projects + 7-day trend
      case NOTE_PAGE: drawNotePage(); break;  // left column + note pane
    }
    drawStatusStrip();
  }
  drawSystemCorner(false);
  unlockState();
  uint32_t drawUs = micros() - startUs;
  presentFrame();
  // Permanent low-volume diagnostic (one line per render, only visible with a
  // serial monitor attached) so a draw-time regression shows up the same way
  // the CYD's did.
  // presents/s is the ambient budget: each present is a whole ~16ms frame.
  static uint32_t lastLogMs = 0, lastLogPresents = 0;
  uint32_t nowMs = millis();
  if (nowMs - lastLogMs >= 10000) {
    uint32_t presents = displayPresentCount();
    float perSec = lastLogMs ? (presents - lastLogPresents) * 1000.0f / (nowMs - lastLogMs) : 0;
    lastLogMs = nowMs;
    lastLogPresents = presents;
    Serial.printf("[timing] render() page=%d draw %luus present %luus (te wait %luus) presents %.1f/s\n",
                  currentPage, (unsigned long)drawUs, (unsigned long)displayLastPresentUs(),
                  (unsigned long)displayLastTeWaitUs(), perSec);
  }
}

// ── MEDIA OVERLAYS (gif_player.cpp) ────────────────────────
// Reset readout on the full-screen cat page and the offline screen: the
// usage % (data.usage) + "Resets 03:19" (primary), headline -- one weight up
// from body, as text over media must be -- on a plate, bottom-left.
void drawResetPlate() {
  lockState();
  int percent = STATE.sessionPercent;
  String resets = STATE.sessionResets;
  unlockState();
  if (percent < 0 || !resets.length()) return;
  String a = String(percent) + "%", b = "Resets " + resets;
  const int padX = TOK_SPACE_SM, padY = 6;
  const int gap = TOK_SPACE_SM;
  int w = textW(TOK_TYPE_HEADLINE, a) + gap + textW(TOK_TYPE_HEADLINE, b) + 2 * padX;
  int h = fontLineH(TOK_TYPE_HEADLINE) + 2 * padY;
  int x = TOK_SPACE_SM, y = SCREEN_H - TOK_SPACE_SM - h;
  aaFillRoundRect(x, y, w, h, TOK_RADIUS_SM, TOK_COLOR_PLATE);
  int tx = drawText(TOK_TYPE_HEADLINE, x + padX, y + padY, a, TOK_COLOR_DATA_USAGE);
  drawText(TOK_TYPE_HEADLINE, tx + gap, y + padY, b, TOK_COLOR_TEXT_PRIMARY);
}
