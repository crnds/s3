// sim/pages.js -- media pages (placeholder cats/movies, pane pages) and render() (pages.cpp).

// ── MEDIA PAGES WITH A STATIC HALF (Mixed, Overview) ──────────
function catPanePage(offline) {
  return !offline && (STATE.currentPage === MIXED_PAGE || STATE.currentPage === OVERVIEW_PAGE);
}
function catPaneRect() {
  return STATE.currentPage === OVERVIEW_PAGE ? [OV_CAT_X0, OV_CAT_Y0, OV_CAT_W, OV_CAT_H]
                                             : [MIXED_GIF_X0, MIXED_GIF_Y0, MIXED_GIF_W, MIXED_GIF_H];
}
// Everything but the cat pane, plus the strip (pages.cpp's drawMediaPageStatic).
function drawMediaPageStatic() {
  const [px, py, pw, ph] = catPaneRect();
  gfx.fillRect(0, 0, SCREEN_W, py, TOK_COLOR_BG_CANVAS);
  gfx.fillRect(0, py + ph, SCREEN_W, SCREEN_H - py - ph, TOK_COLOR_BG_CANVAS);
  gfx.fillRect(0, py, px, ph, TOK_COLOR_BG_CANVAS);
  gfx.fillRect(px + pw, py, SCREEN_W - px - pw, ph, TOK_COLOR_BG_CANVAS);
  if (STATE.currentPage === OVERVIEW_PAGE) drawOverviewCells();
  else { drawLimitsColumn(); drawBtcCard(); }
  drawStatusStrip();
}

// ── CAT PAGES (placeholder -- the firmware-only surface) ──────────────────
// The device plays random GIFs embedded in the firmware (gif_player.cpp); the canvas
// can't, so this shows the firmware's own placeholder layout plus the same
// overlays (reset plate, shuffle button, corner plates).
// Cats are embedded in the firmware, so only the SD-backed movie page depends on the card.
function moviesAvailable() { return !STATE.simSdMissing; }
function drawResetPlate() {
  const percent = STATE.sessionPercent, resets = STATE.sessionResets;
  if (percent < 0 || !resets) return;
  const a = percent + '%', b = 'Resets ' + resets;
  const padX = TOK_SPACE_SM, padY = 6, gap = TOK_SPACE_SM;
  const w = textW(TOK_TYPE_HEADLINE, a) + gap + textW(TOK_TYPE_HEADLINE, b) + 2 * padX;
  const h = fontLineH(TOK_TYPE_HEADLINE) + 2 * padY;
  const x = TOK_SPACE_SM, y = SCREEN_H - TOK_SPACE_SM - h;
  gfx.fillSmoothRoundRect(x, y, w, h, TOK_RADIUS_SM, TOK_COLOR_PLATE);
  const tx = drawText(TOK_TYPE_HEADLINE, x + padX, y + padY, a, TOK_COLOR_DATA_USAGE);
  drawText(TOK_TYPE_HEADLINE, tx + gap, y + padY, b, TOK_COLOR_TEXT_PRIMARY);
}
function drawMediaOverlays(offline) {
  const pane = catPanePage(offline);
  if (!pane) drawResetPlate();
  const movie = STATE.currentPage === MOVIE_PAGE && !offline;
  if (STATE.catShuffleSec < 0 && (!movie || moviesAvailable())) {
    const [cx, cy] = shuffleCentre(offline);
    drawShuffleButton(cx, cy, NAV.pressedId === 'shuffle');
  }
  // The Overview's corner sits over its clock card, not over the cat.
  drawSystemCorner(!(pane && STATE.currentPage === OVERVIEW_PAGE));
}
function drawGifPage(offline) {
  const movie = STATE.currentPage === MOVIE_PAGE && !offline;
  if (catPanePage(offline)) {
    gfx.fillScreen(TOK_COLOR_BG_CANVAS);
    drawMediaPageStatic();
    const [px, py, pw, ph] = catPaneRect();
    drawCardSurface(px, py, pw, ph, TOK_COLOR_SURFACE_CARD);
    drawEmptyState(px + pw / 2, py, ph, 'Cats play here',
                   STATE.currentPage === OVERVIEW_PAGE ? 'GIFs on the device' : 'Cover-fit GIFs on the device', false);
  } else if (movie) {
    // Deliberate non-twin, same as the cats placeholder below: no real MJPEG
    // decode in the browser either (CLAUDE.md's "Deliberate non-twins").
    gfx.fillScreen(TOK_COLOR_BG_CANVAS);
    if (moviesAvailable())
      drawEmptyState(SCREEN_W / 2, 0, SCREEN_H, 'Movies play here', 'Random .mjpeg files from /movies/ on the device', false,
                     TOK_TYPE_DISPLAY, TOK_COLOR_TEXT_SECONDARY);
    else
      drawEmptyState(SCREEN_W / 2, 0, SCREEN_H, 'No SD card', 'Insert an SD card with /movies/ .mjpeg files', true);
  } else {
    gfx.fillScreen(TOK_COLOR_BG_CANVAS);
    drawEmptyState(SCREEN_W / 2, 0, SCREEN_H, 'Cats play here', 'Random embedded GIFs on the device', false,
                   TOK_TYPE_DISPLAY, TOK_COLOR_TEXT_SECONDARY);
  }
  drawMediaOverlays(offline);
}

// ── RENDER (pages.cpp's render(); presents at the end) ──────────
function render() {
  gfx.fillScreen(TOK_COLOR_BG_CANVAS);
  if (STATE.weatherPageOpen) drawWeatherPage();
  else if (STATE.devicePageOpen) drawDevicePage();
  else if (navCatLayout()) { drawGifPage(!STATE.haveData); presentFrame(); return; }
  else {
    switch (STATE.currentPage) {
      case 0: drawStatusPage(); break;
      case 1: drawUsagePage(); break;
      case NOTE_PAGE: drawNotePage(); break;
    }
    drawStatusStrip();
  }
  drawSystemCorner(false);
  presentFrame();
}
