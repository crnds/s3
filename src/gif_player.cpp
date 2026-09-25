// CATS / GIF_PAGE and the MIXED_PAGE cat pane: random cat GIFs from /cats/ on
// the SD card, played endlessly (and full-screen on any page while offline --
// the cats ARE the offline screen, regardless of currentPage: see gifTick()'s
// top). Decode + draw happen on the render core (core 1) in gifTick(); SD
// reads there are guarded by sdMutex so they can't collide with the network
// task's writes.
//
// MOVIE_PAGE (random .mjpeg playback from /movies/) is a sibling module,
// movie_player.cpp, with its own decoder/canvas -- but it shares this file's
// integration points rather than getting its own nav.cpp/main.cpp wiring.
// nav.cpp's isCatPage()/navCatLayout() treat GIF_PAGE/MOVIE_PAGE/MIXED_PAGE
// as one "cat mode" (a pre-existing name that predates movies), so every
// caller here -- gifTick() and the five gifPlayer* entry points below --
// dispatches to movie_player.cpp's equivalents whenever currentPage ==
// MOVIE_PAGE && online, and otherwise runs unchanged. Offline always falls
// through to cats, regardless of currentPage, matching the existing offline
// behaviour above.
//
// What changed from the CYD player: every GIF frame is composited in RAW mode
// into gifCanvas, a GIF-sized RGB565 canvas in PSRAM, and only then copied
// into `frame` -- 1:1 on the full-screen cat page, or cover-fit (uniform
// scale, cropped to fill, never stretched) into the mixed page's right-column
// pane (x 188..471). Overlays (reset plate, shuffle button, corner glyphs)
// always sit on solid plates: nothing is drawn over media without one
// (design.md 11.20).
// Compositing on a canvas that belongs to the GIF (rather than straight onto
// the page) is what keeps transparency/disposal correct under the resize and
// under the overlays drawn on top. The CYD's dirty-band partial pushes are
// gone: the panel only takes whole frames.
//
// Everything below except the functions declared in state.h is file-local.
#include "state.h"

uint32_t catShuffleMs = 0;  // 0 = let each GIF play to its natural end
bool catShuffleFixed = false;  // FIXED preset: never auto-rotate, tap-only advance

// The decoder (and its ~24KB of work buffers) exists only while a cat page is
// showing (gifPlayerEnterCatMode/ExitCatMode), same lifecycle as the CYD.
static AnimatedGIF* gif = nullptr;
static File gifFile;                 // handle the AnimatedGIF file callbacks read through
static const char* CATS_DIR = "/cats";
static const int MAX_CATS = 200;     // cap the in-RAM filename list
static String catFiles[MAX_CATS];
static int catCount = 0;
static bool gifOpen = false;
static bool gifPlaceholderDrawn = false;
static uint32_t gifNextFrameMs = 0;
static uint32_t gifOpenedAtMs = 0;   // when the current GIF opened; used by the shuffle-interval cutoff
static int currentCatIndex = -1;     // currently open cat GIF index
static bool gifMixedMode = false;    // layout the current GIF was opened for
static uint32_t gifFrames = 0;       // decoded-frame counter for the fps log
// Set when a GIF has ended and should replay (Cat Shuffle FIXED, or an
// interval that hasn't elapsed yet) rather than hand over to a random cat.
static bool nextOpenIsLoop = false;

// GIF-space compositing canvas (PSRAM, allocated once, sized for the largest
// canvas AnimatedGIF accepts: MAX_WIDTH 480 x the 320-row screen).
static uint16_t* gifCanvas = nullptr;
static int canvasW = 0, canvasH = 0;
// Bounding box of canvas pixels touched by the current frame (inclusive).
static int dirtyX0, dirtyY0, dirtyX1, dirtyY1;
// Where the canvas lands in `frame`: full-screen 1:1 offset, or the mixed
// pane's origin (cover-fit always fills the whole pane, so no centering
// offset is needed there -- see mixedScale/mixedSrcOffX/Y below).
static int destX = 0, destY = 0;
// Mixed-pane cover-fit, computed once per GIF open (openCatAtIndex): the
// single scale factor (same on both axes, so the aspect ratio never
// distorts) that makes the canvas cover the MIXED_GIF_W x MIXED_GIF_H pane,
// plus the canvas-space top-left of the visible (uncropped) window. Whichever
// axis has leftover after the other is scaled to fit gets cropped -- usually
// the sides, since fit_cats.py enlarges every cat to touch the full-screen
// page's height first.
static float mixedScale = 1.0f;
static float mixedSrcOffX = 0.0f, mixedSrcOffY = 0.0f;

static inline void markDirty(int x0, int x1, int y) {
  if (x0 < dirtyX0) dirtyX0 = x0;
  if (x1 > dirtyX1) dirtyX1 = x1;
  if (y < dirtyY0) dirtyY0 = y;
  if (y > dirtyY1) dirtyY1 = y;
}
static inline void resetDirty() { dirtyX0 = dirtyY0 = INT_MAX; dirtyX1 = dirtyY1 = -1; }

// ── ANIMATEDGIF CALLBACKS ──────────────────────────────────
// The SD reads inside these run under sdMutex, which gifTick()/openCatAtIndex()
// hold around every gif open/playFrame/close.
static void* GIFOpenFile(const char* fname, int32_t* pSize) {
  gifFile = SD_MMC.open(fname);
  if (!gifFile) return nullptr;
  *pSize = gifFile.size();
  return (void*)&gifFile;
}
static void GIFCloseFile(void* pHandle) {
  File* f = static_cast<File*>(pHandle);
  if (f) f->close();
}
static int32_t GIFReadFile(GIFFILE* pFile, uint8_t* pBuf, int32_t iLen) {
  File* f = static_cast<File*>(pFile->fHandle);
  int32_t want = iLen;
  // Reading to the very last byte broke a later seek() on the CYD's SD lib;
  // kept, it costs nothing (the GIF trailer byte is never needed).
  if ((pFile->iSize - pFile->iPos) < iLen) want = pFile->iSize - pFile->iPos - 1;
  if (want <= 0) return 0;
  int32_t got = (int32_t)f->read(pBuf, want);
  pFile->iPos = f->position();
  return got;
}
static int32_t GIFSeekFile(GIFFILE* pFile, int32_t iPosition) {
  File* f = static_cast<File*>(pFile->fHandle);
  f->seek(iPosition);
  pFile->iPos = (int32_t)f->position();
  return pFile->iPos;
}

// One decoded image line into gifCanvas, honouring transparency (transparent
// pixels leave the previous frame's canvas pixel alone) and disposal method 2
// (restore-to-background paints them as the background colour).
static void GIFDraw(GIFDRAW* pDraw) {
  if (!gifCanvas) return;
  int y = pDraw->iY + pDraw->y;
  if (y < 0 || y >= canvasH) return;
  int x0 = pDraw->iX;
  int w = pDraw->iWidth;
  if (x0 < 0 || x0 >= canvasW) return;
  if (x0 + w > canvasW) w = canvasW - x0;
  if (w <= 0) return;

  uint8_t* s = pDraw->pPixels;
  uint16_t* pal = pDraw->pPalette;
  uint16_t* d = gifCanvas + y * canvasW + x0;

  if (pDraw->ucDisposalMethod == 2) {  // restore-to-background: paint transparent as bg
    for (int x = 0; x < w; x++)
      if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground;
    pDraw->ucHasTransparency = 0;
  }

  if (pDraw->ucHasTransparency) {
    uint8_t t = pDraw->ucTransparent;
    int first = -1, last = -1;
    for (int x = 0; x < w; x++) {
      uint8_t c = s[x];
      if (c == t) continue;
      d[x] = pal[c];
      if (first < 0) first = x;
      last = x;
    }
    if (first >= 0) markDirty(x0 + first, x0 + last, y);
  } else {
    for (int x = 0; x < w; x++) d[x] = pal[s[x]];
    markDirty(x0, x0 + w - 1, y);
  }
}

// ── CANVAS -> FRAME ────────────────────────────────────────
// Copy the frame's dirty box from the canvas into `frame`.
static void blitDirty() {
  if (dirtyX1 < 0) return;
  uint16_t* fb = (uint16_t*)frame.getBuffer();
  if (!gifMixedMode) {
    for (int y = dirtyY0; y <= dirtyY1; y++) {
      int dy = destY + y;
      if (dy < 0 || dy >= SCREEN_H) continue;
      int x0 = dirtyX0, x1 = dirtyX1;
      if (destX + x0 < 0) x0 = -destX;
      if (destX + x1 >= SCREEN_W) x1 = SCREEN_W - 1 - destX;
      if (x1 < x0) continue;
      memcpy(fb + dy * SCREEN_W + destX + x0, gifCanvas + y * canvasW + x0, (x1 - x0 + 1) * 2);
    }
    return;
  }
  // Cover-fit into the mixed pane: nearest-sample the canvas through the
  // open-time scale/crop (mixedScale/mixedSrcOffX/Y), touching only the
  // destination pixels whose nearest source pixel falls in this frame's
  // dirty box. Nearest-neighbour, not a box average: the scale here is
  // rarely a clean ratio (it depends on each GIF's own aspect ratio), and at
  // this pane size the aliasing is not visible.
  float invScale = 1.0f / mixedScale;
  int dstX0 = (int)floorf((dirtyX0 - mixedSrcOffX) * mixedScale);
  int dstX1 = (int)ceilf((dirtyX1 + 1 - mixedSrcOffX) * mixedScale) - 1;
  int dstY0 = (int)floorf((dirtyY0 - mixedSrcOffY) * mixedScale);
  int dstY1 = (int)ceilf((dirtyY1 + 1 - mixedSrcOffY) * mixedScale) - 1;
  if (dstX0 < 0) dstX0 = 0;
  if (dstY0 < 0) dstY0 = 0;
  if (dstX1 >= MIXED_GIF_W) dstX1 = MIXED_GIF_W - 1;
  if (dstY1 >= MIXED_GIF_H) dstY1 = MIXED_GIF_H - 1;
  for (int py = dstY0; py <= dstY1; py++) {
    int sy = (int)(mixedSrcOffY + (py + 0.5f) * invScale);
    if (sy < 0) sy = 0;
    if (sy >= canvasH) sy = canvasH - 1;
    const uint16_t* srow = gifCanvas + sy * canvasW;
    uint16_t* drow = fb + (destY + py) * SCREEN_W + destX;
    for (int px = dstX0; px <= dstX1; px++) {
      int sx = (int)(mixedSrcOffX + (px + 0.5f) * invScale);
      if (sx < 0) sx = 0;
      if (sx >= canvasW) sx = canvasW - 1;
      drow[px] = srow[sx];
    }
  }
}

// Scan /cats/ for *.gif once at boot into catFiles[]. Names are normalized to a
// full "/cats/<name>" path (openNextFile()'s name() is basename-only here).
void scanCats() {
  catCount = 0;
  if (!STATE.sdOk) return;
  lockSD();
  File dir = SD_MMC.open(CATS_DIR);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f && catCount < MAX_CATS; f = dir.openNextFile()) {
      if (!f.isDirectory()) {
        String name = f.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        String lower = name; lower.toLowerCase();
        // Skip hidden files: macOS copies to a FAT card leave "._cat_NNN.gif"
        // AppleDouble companions whose names also end in ".gif"; unfiltered
        // they fill the list and then fail to open.
        if (!name.startsWith(".") && lower.endsWith(".gif"))
          catFiles[catCount++] = String(CATS_DIR) + "/" + name;
      }
      f.close();
    }
  }
  if (dir) dir.close();
  unlockSD();
  Serial.printf("[cats] %d GIF(s) in %s\n", catCount, CATS_DIR);
}

// Everything drawn over the cat (or movie) after each frame: the reset plate
// (the full-screen layout -- the cat/movie page, and the offline screen,
// where it and the corner glyphs carry the status), the shuffle media
// control (Cat Shuffle Fixed only), then the system corner on plates. Shared
// with movie_player.cpp (declared in state.h) since MOVIE_PAGE uses the same
// overlays, just counting movies instead of cats.
void drawMediaOverlays(bool offline) {
  bool mixed = (currentPage == MIXED_PAGE && !offline);
  if (!mixed) drawResetPlate();
  bool onMoviePage = (currentPage == MOVIE_PAGE && !offline);
  int count = onMoviePage ? movieCount : catCount;
  if (catShuffleFixed && count > 0) {
    int cx, cy;
    shuffleCentre(mixed, cx, cy);
    drawShuffleButton(cx, cy, pressedId == PRESS_SHUFFLE);
  }
  drawSystemCorner(true);
}

// Empty / error state when there are no cats to show (no SD, or an empty
// /cats/), design.md 13.4. Drawn once per page visit (gifPlaceholderDrawn).
// Returns true when it drew.
static bool drawGifPlaceholder(bool offline) {
  if (gifPlaceholderDrawn) return false;
  gifPlaceholderDrawn = true;
  bool mixedMode = (currentPage == MIXED_PAGE && !offline);
  const bool noSd = !STATE.sdOk;
  if (mixedMode) {
    g->fillRect(MIXED_GIF_X0, MIXED_GIF_Y0, MIXED_GIF_W, MIXED_GIF_H, TOK_COLOR_BG_CANVAS);
    drawCardSurface(MIXED_GIF_X0, MIXED_GIF_Y0, MIXED_GIF_W, MIXED_GIF_H, TOK_COLOR_SURFACE_CARD);
    drawEmptyState(MIXED_GIF_X0 + MIXED_GIF_W / 2, MIXED_GIF_Y0, MIXED_GIF_H,
                   noSd ? "No SD card" : "No cats yet",
                   noSd ? "Insert a card with /cats/ GIFs" : "Add GIFs to /cats/ on the card", noSd);
  } else {
    g->fillScreen(TOK_COLOR_BG_CANVAS);
    if (noSd)
      drawEmptyState(SCREEN_W / 2, 0, SCREEN_H, "No SD card", "Insert an SD card with /cats/ GIFs", true);
    else
      drawEmptyState(SCREEN_W / 2, 0, SCREEN_H, "No cats yet", "Add GIFs to /cats/ on the SD card", false,
                     TOK_TYPE_DISPLAY, TOK_COLOR_TEXT_SECONDARY);
  }
  drawMediaOverlays(offline);
  return true;
}

// Open a cat GIF by index, laid out for the current page (full-screen 1:1,
// or 2x down into the mixed pane), and clear where it will draw.
static bool openCatAtIndex(int index, bool resetOpenedTime) {
  if (catCount == 0 || !gif || !gifCanvas || index < 0 || index >= catCount) return false;
  gif->begin(GIF_PALETTE_RGB565_BE);  // big-endian RGB565 = the sprite's own byte order
  lockSD();
  int ok = gif->open(catFiles[index].c_str(), GIFOpenFile, GIFCloseFile,
                     GIFReadFile, GIFSeekFile, GIFDraw);
  unlockSD();
  if (!ok) return false;
  canvasW = min(gif->getCanvasWidth(), SCREEN_W);
  canvasH = min(gif->getCanvasHeight(), SCREEN_H);
  memset(gifCanvas, 0, (size_t)canvasW * canvasH * 2);
  bool offline = !STATE.haveData;
  gifMixedMode = (currentPage == MIXED_PAGE && !offline);
  if (gifMixedMode) {
    // Cover fit: the larger of the two axis ratios wins, so the pane is
    // always fully covered and the other axis crops instead of letterboxing.
    // Same scale on both axes -- the aspect ratio is never distorted.
    mixedScale = max((float)MIXED_GIF_W / canvasW, (float)MIXED_GIF_H / canvasH);
    float visW = MIXED_GIF_W / mixedScale, visH = MIXED_GIF_H / mixedScale;
    mixedSrcOffX = (canvasW - visW) / 2.0f;
    mixedSrcOffY = (canvasH - visH) / 2.0f;
    destX = MIXED_GIF_X0;
    destY = MIXED_GIF_Y0;
    g->fillRect(MIXED_GIF_X0, MIXED_GIF_Y0, MIXED_GIF_W, MIXED_GIF_H, TOK_COLOR_PLATE);  // clear only the pane
  } else {
    destX = (SCREEN_W - canvasW) / 2;
    destY = (SCREEN_H - canvasH) / 2;
    g->fillScreen(TOK_COLOR_PLATE);  // smaller (legacy 320x240) GIFs letterbox on black
  }
  gifOpen = true;
  if (resetOpenedTime) gifOpenedAtMs = millis();
  return true;
}

static bool openRandomCat() {
  currentCatIndex = (int)random(catCount);
  return openCatAtIndex(currentCatIndex, true);
}

// Reopen the currently active cat GIF to loop it, keeping gifOpenedAtMs.
static bool reopenCurrentCat() {
  return openCatAtIndex(currentCatIndex, false);
}

static void closeGif() {
  if (gif && gifOpen) { lockSD(); gif->close(); unlockSD(); }
  gifOpen = false;
}

// Called from loop() on core 1 while a cat page is showing, OR while offline on
// any page. Decodes at most one frame per call, paced by the GIF's own frame
// delays; when a GIF ends it opens another at random -- endless cats. Returns
// true when it changed `frame` (loop() then presents).
bool gifTick(bool offline) {
  if (currentPage == MOVIE_PAGE && !offline) return movieTick(offline);
  if (!STATE.sdOk || catCount == 0 || !gif || !gifCanvas) return drawGifPlaceholder(offline);
  uint32_t now = millis();

  // Layout flipped under an open GIF (went offline / came back while on the
  // mixed page): reopen it for the new layout.
  bool wantMixed = (currentPage == MIXED_PAGE && !offline);
  if (gifOpen && wantMixed != gifMixedMode) {
    closeGif();
    nextOpenIsLoop = true;   // same cat, new layout
    gifNextFrameMs = now;    // and decode it this tick, see below
  }

  // One gate paces both frames and the gap after a GIF's last frame, so the
  // last frame is held for its own delay before the next GIF starts.
  // (int32_t) subtraction wraps correctly across millis()'s rollover.
  if ((int32_t)(now - gifNextFrameMs) < 0) return false;

  // Opening clears the canvas and its screen area to black, so it must be
  // followed by the first frame's decode in the SAME tick: the caller
  // presents whatever this returns, and a present between the two is the
  // one-frame black flash the loop restart used to show.
  if (!gifOpen) {
    bool ok = nextOpenIsLoop ? reopenCurrentCat() : openRandomCat();
    nextOpenIsLoop = false;
    if (!ok && !openRandomCat()) {
      bool drew = drawGifPlaceholder(offline);
      gifNextFrameMs = now + 1000;  // retry opening later
      return drew;
    }
  }
  now = millis();  // opening performs slow SD I/O
  int delayMs = 0;
  // Bounded wait: if networkTask (core 0) is mid-poll on the card, drop this
  // frame rather than blocking the render core -- during an outage the cat
  // player and the recovery poll compete for the card, and the poll must win.
  if (!tryLockSD(20)) {
    gifNextFrameMs = now + 20;
    return false;
  }
  resetDirty();
  int more = gif->playFrame(false, &delayMs);  // bSync=false: we handle timing ourselves
  unlockSD();
  blitDirty();
  gifFrames++;

  drawMediaOverlays(offline);

  // Rotate to a new random cat at the GIF's natural end, or early when the
  // Cat Shuffle interval says this one has played long enough. FIXED
  // (catShuffleFixed) just keeps replaying the current cat until a tap.
  // GIF frame delays are honoured as-is (no 12fps ceiling any more); 0 is
  // the conventional "unspecified" and plays like a browser's 100ms-ish.
  uint32_t hold = delayMs > 0 ? (uint32_t)max(delayMs, 15) : 80;
  bool shuffleDue = !catShuffleFixed && catShuffleMs > 0 && now >= gifOpenedAtMs &&
                    (now - gifOpenedAtMs) >= catShuffleMs;
  if (shuffleDue || (more == 0 && !catShuffleFixed && catShuffleMs == 0)) {
    // Hand over to a random cat, opened (and first-frame decoded) by the tick
    // that passes the gate: straight away for a shuffle cut, after the last
    // frame's own delay at a natural end.
    closeGif();
    nextOpenIsLoop = false;
    gifNextFrameMs = shuffleDue ? now : now + hold;
  } else if (more == 0) {
    closeGif();
    nextOpenIsLoop = true;         // replay the same cat after the last frame's delay
    gifNextFrameMs = now + hold;
  } else {
    gifNextFrameMs = now + hold;
  }

  static uint32_t lastFpsLogMs = 0, lastFpsFrames = 0;
  if (now - lastFpsLogMs >= 60000) {
    Serial.printf("[cats] %.1f fps decoded\n", (gifFrames - lastFpsFrames) * 1000.0f / (now - lastFpsLogMs));
    lastFpsLogMs = now;
    lastFpsFrames = gifFrames;
  }
  return true;
}

// Allocate the decoder only while it's needed (entering a cat page, or going
// offline on any page) and reset the placeholder/timer for the fresh visit.
// Also lazily allocates the movie decoder: isCatPage() treats GIF_PAGE/
// MOVIE_PAGE/MIXED_PAGE as one mode, so swiping straight from Cats to Movies
// (or vice versa) never re-fires this function -- entering "cat mode" from
// any other page must get both decoders ready up front.
void gifPlayerEnterCatMode() {
  if (!gifCanvas)
    gifCanvas = (uint16_t*)heap_caps_malloc((size_t)SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
  if (!gif) gif = new AnimatedGIF();
  gifPlaceholderDrawn = false;
  nextOpenIsLoop = false;
  gifNextFrameMs = millis();
  moviePlayerEnter();
}

// Leaving cat mode: close any open GIF and free the decoder. The PSRAM canvas
// stays (8MB is plenty, and re-allocating 300KB per visit buys nothing).
void gifPlayerExitCatMode() {
  closeGif();
  delete gif;
  gif = nullptr;
  moviePlayerExit();
}

// Force the next gifTick() to open a fresh (random) GIF -- page changes onto
// GIF_PAGE/MIXED_PAGE, and the shuffle media control. On MOVIE_PAGE, hand
// this over to the movie player's equivalent instead (its own random-pick
// state is separate from the GIF path's).
void gifPlayerResetForPageChange() {
  if (currentPage == MOVIE_PAGE && STATE.haveData) { moviePlayerResetForPageChange(); return; }
  closeGif();
  gifPlaceholderDrawn = false;
  nextOpenIsLoop = false;
  gifNextFrameMs = millis();
}

// Draw the first frame immediately (page slide renders the incoming page
// before animating to it).
void gifPlayerPrimeFrame(bool offline) {
  if (currentPage == MOVIE_PAGE && !offline) { moviePlayerPrimeFrame(offline); return; }
  gifNextFrameMs = millis();  // "due now" (0 would read as far future after ~24.8 days' uptime)
  gifTick(offline);
}

// Re-blit the whole open canvas into `frame` (plus the mixed page's static
// half and the overlays) -- a sheet or a cancelled page lean overwrote it.
// With no GIF open (between cats, or none on the card) it falls back to a
// fresh open, which clears and decodes in one tick.
void gifPlayerRepaint(bool offline) {
  if (currentPage == MOVIE_PAGE && !offline) { moviePlayerRepaint(offline); return; }
  bool mixed = (currentPage == MIXED_PAGE && !offline);
  if (mixed) {
    lockState();
    drawMixedPageStatic();
    unlockState();
  }
  if (!gif || !gifOpen || gifMixedMode != mixed || !gifCanvas) {
    gifPlayerResetForPageChange();
    gifPlayerPrimeFrame(offline);
    return;
  }
  if (!mixed) g->fillScreen(TOK_COLOR_PLATE);
  dirtyX0 = 0; dirtyY0 = 0; dirtyX1 = canvasW - 1; dirtyY1 = canvasH - 1;
  blitDirty();
  drawMediaOverlays(offline);
  presentFrame();
}

void gifPlayerRedrawOverlays(bool offline) {
  drawMediaOverlays(offline);
  presentFrame();
}
