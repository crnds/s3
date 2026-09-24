// CATS / GIF_PAGE and the MIXED_PAGE cat pane: random cat GIFs from /cats/ on
// the SD card, played endlessly (and full-screen on any page while offline --
// the cats ARE the offline screen). Decode + draw happen on the render core
// (core 1) in gifTick(); SD reads there are guarded by sdMutex so they can't
// collide with the network task's writes.
//
// What changed from the CYD player: every GIF frame is composited in RAW mode
// into gifCanvas, a GIF-sized RGB565 canvas in PSRAM, and only then copied
// into `frame` -- 1:1 on the full-screen cat page, or 2x box-filtered into the
// mixed page's 240px pane. Compositing on a canvas that belongs to the GIF
// (rather than straight onto the page) is what keeps transparency/disposal
// correct under the downscale and under the overlays drawn on top. The CYD's
// dirty-band partial pushes are gone: the panel only takes whole frames.
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
// pane's 2x-downscaled offset.
static int destX = 0, destY = 0;

static void drawSessionResetOverlay();

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
// Byte-swapped RGB565 (the canvas and the sprite both hold big-endian pixels)
// averaged per channel over a 2x2 block.
static inline uint16_t avg4(uint16_t a, uint16_t b, uint16_t c, uint16_t d) {
  a = (a >> 8) | (a << 8); b = (b >> 8) | (b << 8);
  c = (c >> 8) | (c << 8); d = (d >> 8) | (d << 8);
  uint32_t r = ((a >> 11) + (b >> 11) + (c >> 11) + (d >> 11) + 2) >> 2;
  uint32_t gg = (((a >> 5) & 0x3F) + ((b >> 5) & 0x3F) + ((c >> 5) & 0x3F) + ((d >> 5) & 0x3F) + 2) >> 2;
  uint32_t bl = ((a & 0x1F) + (b & 0x1F) + (c & 0x1F) + (d & 0x1F) + 2) >> 2;
  uint16_t o = (uint16_t)((r << 11) | (gg << 5) | bl);
  return (o >> 8) | (o << 8);
}

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
  // 2x box filter into the mixed pane. Snap the dirty box outward to even
  // canvas coordinates so every output pixel averages a whole 2x2 block.
  int x0 = dirtyX0 & ~1, y0 = dirtyY0 & ~1;
  int x1 = dirtyX1 | 1, y1 = dirtyY1 | 1;
  if (x1 >= canvasW) x1 = canvasW - 1;
  if (y1 >= canvasH) y1 = canvasH - 1;
  for (int y = y0; y + 1 <= y1; y += 2) {
    int dy = destY + y / 2;
    if (dy < MIXED_GIF_Y0 || dy >= MIXED_GIF_Y0 + MIXED_GIF_H) continue;
    const uint16_t* r0 = gifCanvas + y * canvasW;
    const uint16_t* r1 = r0 + canvasW;
    uint16_t* d = fb + dy * SCREEN_W + destX;
    for (int x = x0; x + 1 <= x1; x += 2) {
      int dx = destX + x / 2;
      if (dx < MIXED_GIF_X0 || dx >= MIXED_GIF_X0 + MIXED_GIF_W) continue;
      d[x / 2] = avg4(r0[x], r0[x + 1], r1[x], r1[x + 1]);
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

// "26% reset: 02:09" pinned to the bottom-left corner of the full-screen cat
// page (GIF_PAGE only, never while offline), on a solid black box so it stays
// legible over any frame. gifTick() runs unlocked, so STATE is copied first.
static void drawSessionResetOverlay() {
  lockState();
  int percent = STATE.sessionPercent;
  String resets = STATE.sessionResets;
  unlockState();
  if (percent < 0 || !resets.length()) return;

  String a = String(percent) + "% ", b = "reset: ";
  const int padX = 8, padY = 6;
  int textW_ = textW(FONT_MD, a) + textW(FONT_MD, b) + textW(FONT_MD, resets);
  int boxW = textW_ + padX * 2, boxH = fontLineH(FONT_MD) + padY * 2;
  int boxY = SCREEN_H - boxH;
  g->fillRect(0, boxY, boxW, boxH, 0x0000);
  int x = drawText(FONT_MD, padX, boxY + padY, a, COL_TEXT);
  x = drawText(FONT_MD, x, boxY + padY, b, COL_TEXT2);
  drawText(FONT_MD, x, boxY + padY, resets, COL_TEXT);
}

// A centred message when there are no cats to show (no SD, or empty /cats/).
// Drawn once per page visit (gifPlaceholderDrawn). Returns true when it drew.
static bool drawGifPlaceholder(bool offline) {
  if (gifPlaceholderDrawn) return false;
  gifPlaceholderDrawn = true;
  bool mixedMode = (currentPage == MIXED_PAGE && !offline);
  if (mixedMode) {
    g->fillRect(MIXED_GIF_X0, 0, MIXED_GIF_W, CONTENT_Y1, COL_BG);
    const int cx = MIXED_GIF_X0 + MIXED_GIF_W / 2;
    drawTextC(FONT_LG, cx, 118, "CATS", COL_ACCENT);
    drawTextC(FONT_SM, cx, 158, "no GIFs", COL_TEXT2);
  } else {
    g->fillScreen(COL_BG);
    drawTextC(FONT_XL, SCREEN_W / 2, 118, "CATS", COL_ACCENT);
    drawTextC(FONT_SM, SCREEN_W / 2, 176,
              STATE.sdOk ? "no GIFs found in /cats/ on the SD card"
                         : "insert an SD card with /cats/ GIFs",
              COL_TEXT2);
  }
  if (currentPage == GIF_PAGE && !offline) drawSessionResetOverlay();
  drawBatterySaveIcon();
  drawSleepButton();
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
    destX = MIXED_GIF_X0 + (MIXED_GIF_W - canvasW / 2) / 2;
    destY = MIXED_GIF_Y0 + (MIXED_GIF_H - canvasH / 2) / 2;
    g->fillRect(MIXED_GIF_X0, 0, MIXED_GIF_W, CONTENT_Y1, 0x0000);  // clear only the pane
  } else {
    destX = (SCREEN_W - canvasW) / 2;
    destY = (SCREEN_H - canvasH) / 2;
    g->fillScreen(0x0000);  // smaller (legacy 320x240) GIFs letterbox on black
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

  if (currentPage == GIF_PAGE && !offline) drawSessionResetOverlay();
  drawBatterySaveIcon();
  drawSleepButton();

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
void gifPlayerEnterCatMode() {
  if (!gifCanvas)
    gifCanvas = (uint16_t*)heap_caps_malloc((size_t)SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
  if (!gif) gif = new AnimatedGIF();
  gifPlaceholderDrawn = false;
  nextOpenIsLoop = false;
  gifNextFrameMs = millis();
}

// Leaving cat mode: close any open GIF and free the decoder. The PSRAM canvas
// stays (8MB is plenty, and re-allocating 300KB per visit buys nothing).
void gifPlayerExitCatMode() {
  closeGif();
  delete gif;
  gif = nullptr;
}

// Force the next gifTick() to open a fresh (random) GIF -- page changes onto
// GIF_PAGE/MIXED_PAGE, and the FIXED-shuffle middle tap.
void gifPlayerResetForPageChange() {
  closeGif();
  gifPlaceholderDrawn = false;
  nextOpenIsLoop = false;
  gifNextFrameMs = millis();
}

// Draw the first frame immediately (page slide renders the incoming page
// before animating to it).
void gifPlayerPrimeFrame(bool offline) {
  gifNextFrameMs = millis();  // "due now" (0 would read as far future after ~24.8 days' uptime)
  gifTick(offline);
}
