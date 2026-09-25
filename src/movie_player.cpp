// MOVIE_PAGE: random .mjpeg playback from /movies/ on the SD card, the same
// shape as gif_player.cpp's cat player but for raw MJPEG streams (back-to-
// back baseline JPEG frames, no container -- esp32-s3-spec.md). This module
// is never called directly from nav.cpp/main.cpp; gif_player.cpp's gifTick()
// and its five gifPlayer* entry points dispatch here whenever
// currentPage == MOVIE_PAGE && online (see the header comment at the top of
// gif_player.cpp). Movies are full-screen only -- no mixed-pane variant --
// so there's none of the GIF path's cover-fit/dirty-rect machinery here.
//
// Folder is /movies (not esp32-s3-spec.md's /mjpeg): that name describes the
// seller's own separate reference demo, not this firmware, and the user
// explicitly asked for /movies.
//
// Frame shuffling reuses catShuffleMs/catShuffleFixed (the "Cat Shuffle"
// setting) directly -- the user asked for movies to rotate on the same
// timing as cats, not a second setting.
#include "state.h"
#include "movie_player.h"

// Cat Shuffle's globals live in gif_player.cpp; declared in state.h.

static const char* MOVIES_DIR = "/movies";
static const int MAX_MOVIES = 100;  // cap the in-RAM filename list
static String movieFiles[MAX_MOVIES];
int movieCount = 0;  // extern'd in state.h; gif_player.cpp's drawMediaOverlays reads it

static JPEGDEC* jpeg = nullptr;
static File movieFile;
static bool movieOpen = false;
static bool moviePlaceholderDrawn = false;
static uint32_t movieNextFrameMs = 0;
static uint32_t movieOpenedAtMs = 0;   // when the current movie opened; the shuffle-interval cutoff
static int currentMovieIndex = -1;
static uint32_t movieFrames = 0;       // decoded-frame counter for the fps log
// Set when a movie has ended and should replay (Cat Shuffle FIXED, or an
// interval that hasn't elapsed yet) rather than hand over to a random movie.
static bool nextOpenIsLoop = false;

// Movie-space compositing canvas (PSRAM, allocated once, sized for the
// largest frame this firmware's screen can show: 480x320). Own buffer,
// deliberately not shared with gif_player.cpp's gifCanvas -- the two
// decoders are otherwise independent, and 8MB PSRAM has room for both
// (see esp32-s3-spec.md / CLAUDE.md's PSRAM accounting).
static uint16_t* movieCanvas = nullptr;
static int movieCanvasW = 0, movieCanvasH = 0;
static bool moviePendingFirstFrame = false;  // next decode should set canvas size + clear
// Full-screen 1:1 offset (centres a frame smaller than the screen; the spec
// requires exactly 480x320, so this is normally 0,0).
static int destX = 0, destY = 0;

// Scratch buffer for the current frame's raw compressed JPEG bytes. MJPEG has
// no per-frame length header, so a frame is delimited by scanning for
// FFD8...FFD9; this doubles as the overflow guard matching the spec's 300KB
// largest-frame ceiling.
static const int MOVIE_BUF_CAP = 300 * 1024;
static uint8_t* movieBuf = nullptr;

// ── JPEGDEC CALLBACK ───────────────────────────────────────
// One decoded MCU block into movieCanvas, analogous to gif_player.cpp's
// GIFDraw. JPEGDRAW's pPixels is iWidth wide per row (not iWidthUsed); only
// iWidthUsed columns are valid on a right-edge block.
//
// Decodes as RGB565_LITTLE_ENDIAN and byte-swaps per pixel here, rather than
// asking JPEGDEC for RGB565_BIG_ENDIAN directly: the library's big-endian
// output has a confirmed decode bug (reproduced standalone against upstream
// JPEGDEC off-device, independent of this board/ESP32-S3 -- verified NOT a
// SIMD issue; little-endian output is correct). The sprite's buffer is
// big-endian RGB565 (matching the GIF path's GIF_PALETTE_RGB565_BE), so the
// swap happens on the way into movieCanvas instead.
static int JPEGDraw(JPEGDRAW* pDraw) {
  if (!movieCanvas) return 0;
  int y = pDraw->y;
  if (y < 0 || y >= movieCanvasH) return 1;
  int x0 = pDraw->x;
  if (x0 < 0 || x0 >= movieCanvasW) return 1;
  int w = pDraw->iWidthUsed > 0 ? pDraw->iWidthUsed : pDraw->iWidth;
  if (x0 + w > movieCanvasW) w = movieCanvasW - x0;
  int h = pDraw->iHeight;
  if (y + h > movieCanvasH) h = movieCanvasH - y;
  if (w <= 0 || h <= 0) return 1;
  for (int row = 0; row < h; row++) {
    uint16_t* dst = movieCanvas + (size_t)(y + row) * movieCanvasW + x0;
    const uint16_t* src = pDraw->pPixels + (size_t)row * pDraw->iWidth;
    for (int col = 0; col < w; col++) dst[col] = __builtin_bswap16(src[col]);
  }
  return 1;
}

// Scan /movies/ for *.mjpeg once at boot into movieFiles[]. Mirrors
// gif_player.cpp's scanCats().
void scanMovies() {
  movieCount = 0;
  if (!STATE.sdOk) return;
  lockSD();
  File dir = SD_MMC.open(MOVIES_DIR);
  if (dir && dir.isDirectory()) {
    for (File f = dir.openNextFile(); f && movieCount < MAX_MOVIES; f = dir.openNextFile()) {
      if (!f.isDirectory()) {
        String name = f.name();
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        String lower = name; lower.toLowerCase();
        // Skip hidden files, same reason as scanCats(): macOS AppleDouble
        // companions ("._movie.mjpeg") would otherwise fill the list and
        // then fail to open.
        if (!name.startsWith(".") && lower.endsWith(".mjpeg"))
          movieFiles[movieCount++] = String(MOVIES_DIR) + "/" + name;
      }
      f.close();
    }
  }
  if (dir) dir.close();
  unlockSD();
  Serial.printf("[movies] %d movie(s) in %s\n", movieCount, MOVIES_DIR);
}

// ── CANVAS -> FRAME ────────────────────────────────────────
static void blitMovieCanvas() {
  uint16_t* fb = (uint16_t*)frame.getBuffer();
  for (int y = 0; y < movieCanvasH; y++) {
    int dy = destY + y;
    if (dy < 0 || dy >= SCREEN_H) continue;
    memcpy(fb + (size_t)dy * SCREEN_W + destX, movieCanvas + (size_t)y * movieCanvasW,
           (size_t)movieCanvasW * 2);
  }
}

// Read the next back-to-back JPEG frame (FFD8...FFD9) from movieFile into
// movieBuf, one byte at a time. Returns the frame length, or 0 at end of
// file / on a frame exceeding MOVIE_BUF_CAP (treated as corrupt/EOF by the
// caller). Deliberately avoids seek(): the file position naturally lands
// exactly at the next frame's FFD8 once this returns, no rewind needed.
static int readNextFrame() {
  int len = 0;
  bool sawFF = false;
  while (len < MOVIE_BUF_CAP) {
    int c = movieFile.read();
    if (c < 0) return 0;  // EOF before a full frame
    uint8_t b = (uint8_t)c;
    movieBuf[len++] = b;
    if (sawFF && b == 0xD9) return len;
    sawFF = (b == 0xFF);
  }
  return 0;
}

static bool openMovieAtIndex(int index, bool resetOpenedTime) {
  if (movieCount == 0 || index < 0 || index >= movieCount) return false;
  lockSD();
  movieFile = SD_MMC.open(movieFiles[index].c_str());
  bool ok = (bool)movieFile;
  unlockSD();
  if (!ok) return false;
  movieOpen = true;
  moviePendingFirstFrame = true;
  if (resetOpenedTime) movieOpenedAtMs = millis();
  return true;
}

static bool openRandomMovie() {
  currentMovieIndex = (int)random(movieCount);
  return openMovieAtIndex(currentMovieIndex, true);
}

// Reopen the currently active movie to replay it from the start (MJPEG has
// no per-frame seek table, so "loop" just means reopening the file).
static bool reopenCurrentMovie() {
  return openMovieAtIndex(currentMovieIndex, false);
}

static void closeMovie() {
  if (movieOpen) { lockSD(); movieFile.close(); unlockSD(); }
  movieOpen = false;
}

// Empty / error state when there's nothing to show (no SD, or an empty
// /movies/), mirroring gif_player.cpp's drawGifPlaceholder. Movies are
// full-screen only, so there's no mixed-pane branch.
static bool drawMoviePlaceholder(bool offline) {
  if (moviePlaceholderDrawn) return false;
  moviePlaceholderDrawn = true;
  const bool noSd = !STATE.sdOk;
  g->fillScreen(TOK_COLOR_BG_CANVAS);
  if (noSd)
    drawEmptyState(SCREEN_W / 2, 0, SCREEN_H, "No SD card", "Insert an SD card with /movies/ .mjpeg files", true);
  else
    drawEmptyState(SCREEN_W / 2, 0, SCREEN_H, "No movies yet", "Add .mjpeg files to /movies/ on the SD card", false,
                   TOK_TYPE_DISPLAY, TOK_COLOR_TEXT_SECONDARY);
  drawMediaOverlays(offline);
  return true;
}

// Read, decode and blit exactly one JPEG frame. Sets *eof when the stream
// ended (or a frame was corrupt/oversized) rather than producing a frame.
static bool decodeOneFrame(bool* eof) {
  *eof = false;
  int len = readNextFrame();
  if (len <= 0) { *eof = true; return false; }
  jpeg->setPixelType(RGB565_LITTLE_ENDIAN);  // JPEGDraw() byte-swaps to big-endian; see its comment
  if (!jpeg->openRAM(movieBuf, len, JPEGDraw)) { jpeg->close(); *eof = true; return false; }
  if (moviePendingFirstFrame) {
    movieCanvasW = min(jpeg->getWidth(), SCREEN_W);
    movieCanvasH = min(jpeg->getHeight(), SCREEN_H);
    destX = (SCREEN_W - movieCanvasW) / 2;
    destY = (SCREEN_H - movieCanvasH) / 2;
    memset(movieCanvas, 0, (size_t)SCREEN_W * SCREEN_H * 2);
    g->fillScreen(TOK_COLOR_PLATE);  // smaller-than-screen frames letterbox on the plate colour
    moviePendingFirstFrame = false;
  }
  bool ok = jpeg->decode(0, 0, 0);
  jpeg->close();
  if (!ok) { *eof = true; return false; }
  blitMovieCanvas();
  return true;
}

// Called from gifTick() (gif_player.cpp) once per loop pass while MOVIE_PAGE
// is showing and online. Decodes at most one frame per call, paced to the
// spec's 25fps ceiling; when a movie ends it opens another at random --
// endless movies. Returns true when it changed `frame`.
bool movieTick(bool offline) {
  if (!STATE.sdOk || movieCount == 0 || !jpeg || !movieCanvas || !movieBuf)
    return drawMoviePlaceholder(offline);
  uint32_t now = millis();
  if ((int32_t)(now - movieNextFrameMs) < 0) return false;

  if (!movieOpen) {
    bool ok = nextOpenIsLoop ? reopenCurrentMovie() : openRandomMovie();
    nextOpenIsLoop = false;
    if (!ok && !openRandomMovie()) {
      bool drew = drawMoviePlaceholder(offline);
      movieNextFrameMs = now + 1000;  // retry opening later
      return drew;
    }
  }
  now = millis();  // opening performs slow SD I/O
  // Bounded wait, same reasoning as gifTick(): don't stall a recovery poll.
  if (!tryLockSD(20)) {
    movieNextFrameMs = now + 20;
    return false;
  }
  bool eof = false;
  bool drew = decodeOneFrame(&eof);
  unlockSD();
  if (drew) movieFrames++;

  drawMediaOverlays(offline);

  // 25fps ceiling (esp32-s3-spec.md): never decode faster than this, but
  // never drop frames either -- a slow decode just plays back slower.
  const uint32_t hold = 40;
  bool shuffleDue = !catShuffleFixed && catShuffleMs > 0 && now >= movieOpenedAtMs &&
                    (now - movieOpenedAtMs) >= catShuffleMs;
  if (shuffleDue || (eof && !catShuffleFixed && catShuffleMs == 0)) {
    closeMovie();
    nextOpenIsLoop = false;
    movieNextFrameMs = shuffleDue ? now : now + hold;
  } else if (eof) {
    closeMovie();
    nextOpenIsLoop = true;  // replay the same movie after this tick's hold
    movieNextFrameMs = now + hold;
  } else {
    movieNextFrameMs = now + hold;
  }

  static uint32_t lastFpsLogMs = 0, lastFpsFrames = 0;
  if (now - lastFpsLogMs >= 60000) {
    Serial.printf("[movies] %.1f fps decoded\n", (movieFrames - lastFpsFrames) * 1000.0f / (now - lastFpsLogMs));
    lastFpsLogMs = now;
    lastFpsFrames = movieFrames;
  }
  return drew;
}

// Allocate the decoder + buffers only once (they persist for the rest of the
// session, same reasoning as gifCanvas: 8MB PSRAM is plenty). Called from
// gifPlayerEnterCatMode() every time "cat mode" as a whole is entered, since
// swiping between GIF_PAGE/MOVIE_PAGE/MIXED_PAGE never re-fires that
// function (see the comment there).
void moviePlayerEnter() {
  if (!movieCanvas)
    movieCanvas = (uint16_t*)heap_caps_malloc((size_t)SCREEN_W * SCREEN_H * 2, MALLOC_CAP_SPIRAM);
  if (!movieBuf)
    movieBuf = (uint8_t*)heap_caps_malloc((size_t)MOVIE_BUF_CAP, MALLOC_CAP_SPIRAM);
  if (!jpeg) jpeg = new JPEGDEC();
  moviePlaceholderDrawn = false;
  nextOpenIsLoop = false;
  movieNextFrameMs = millis();
}

// Leaving cat mode: close any open movie and free the (tiny) decoder object.
// The PSRAM canvas/scratch buffer stay allocated.
void moviePlayerExit() {
  closeMovie();
  delete jpeg;
  jpeg = nullptr;
}

// Force the next movieTick() to open a fresh (random) movie -- page changes
// onto MOVIE_PAGE, and the shuffle media control.
void moviePlayerResetForPageChange() {
  closeMovie();
  moviePlaceholderDrawn = false;
  nextOpenIsLoop = false;
  movieNextFrameMs = millis();
}

// Draw the first frame immediately (page slide renders the incoming page
// before animating to it).
void moviePlayerPrimeFrame(bool offline) {
  movieNextFrameMs = millis();
  movieTick(offline);
}

// Re-blit the last decoded frame into `frame` -- a sheet or a cancelled page
// lean overwrote it. With no movie open it falls back to a fresh open, which
// clears and decodes in one tick, same as gifPlayerRepaint().
void moviePlayerRepaint(bool offline) {
  if (!jpeg || !movieOpen || !movieCanvas) {
    moviePlayerResetForPageChange();
    moviePlayerPrimeFrame(offline);
    return;
  }
  g->fillScreen(TOK_COLOR_PLATE);
  blitMovieCanvas();
  drawMediaOverlays(offline);
  presentFrame();
}
