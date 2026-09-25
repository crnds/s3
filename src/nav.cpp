// Navigation: the touch router, the gesture recogniser and every transition
// (design.md sections 9, 12 and 13.1). Twin of simulator-s3.html's NAV block.
//
//   carousel (X)   page halves (commit on up, the lean is the pressed state)
//                  + horizontal swipe, spring slide, velocity handoff
//   sheets (Y)     Weather / Device Stats / Settings rise from the bottom over
//                  a stepped scrim; close glyph, tap-anywhere (read-only
//                  sheets) or drag down dismisses
//   Settings (X)   push a detail from the right; back / swipe right pops
//   lists          1:1 scroll, rubber band past the ends, momentum
//   Reduce Motion  every slide and sheet becomes a 3-frame cross-fade; the
//                  drags that would move a surface are off (each has a tap)
//
// No transition blocks: loop() calls navTouch() then navTick() every pass, so
// a touch during motion is always read. Only one transition runs at a time.
#include "state.h"

PressId pressedId = PRESS_NONE;
int pressedIndex = -1;
extern bool transitionDirty;  // pages.cpp: a present was asked for mid-transition

// ── FRAME BUFFERS ──────────────────────────────────────────
// prevFrame: the outgoing page / screen of a slide, a push or a fade, or the
// sheet itself while it drops. behindFrame: the page under an open sheet,
// captured when the sheet opens (shown while it rises or is dragged).
static uint16_t* prevFrame = nullptr;
static uint16_t* behindFrame = nullptr;
static const size_t FRAME_BYTES = SCREEN_W * SCREEN_H * 2;
static uint16_t* fb() { return (uint16_t*)frame.getBuffer(); }
static bool ensureBuffers() {
  if (!prevFrame) prevFrame = (uint16_t*)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  if (!behindFrame) behindFrame = (uint16_t*)heap_caps_malloc(FRAME_BYTES, MALLOC_CAP_SPIRAM);
  return prevFrame && behindFrame;
}

// ── CAT MODE ───────────────────────────────────────────────
// "Cat mode" now covers all three full-bleed media pages (cats, movies, and
// the mixed cats pane) -- the name predates MOVIE_PAGE and isn't worth
// renaming everywhere it's used, but isCatPage()/navCatLayout() are the
// single gate every caller below keys off, so widening this one check is
// enough: gif_player.cpp's entry points (gifTick and friends) each dispatch
// internally to movie_player.cpp when currentPage == MOVIE_PAGE.
static bool prevCatMode = false;
static bool isCatPage(int p) { return p == GIF_PAGE || p == MOVIE_PAGE || p == MIXED_PAGE; }
bool navCatLayout() { return isCatPage(currentPage) || !STATE.haveData; }
void navSyncCatMode(bool catMode) {
  if (catMode == prevCatMode) return;
  if (catMode) gifPlayerEnterCatMode();
  else gifPlayerExitCatMode();
  prevCatMode = catMode;
}

// Compose currentPage into `frame` without presenting (cat pages open a GIF
// and decode its first frame).
static void preparePage() {
  bool held = presentHold;
  presentHold = true;
  bool offline = !STATE.haveData;
  bool cat = navCatLayout();
  navSyncCatMode(cat);
  if (cat) {
    if (currentPage == MIXED_PAGE && !offline) {
      lockState();
      g->fillScreen(TOK_COLOR_BG_CANVAS);
      drawMixedPageStatic();
      unlockState();
    }
    gifPlayerResetForPageChange();
    gifPlayerPrimeFrame(offline);
  } else {
    render();
  }
  presentHold = held;
}

// Redraw whatever is on screen now (pressed states change on the next present).
static void redrawScreen() {
  if (settingsScreen != SET_OFF) { renderSettings(); return; }
  if (weatherPageOpen || devicePageOpen) { render(); return; }
  if (navCatLayout()) {
    bool offline = !STATE.haveData;
    if (currentPage == MIXED_PAGE && !offline) {
      lockState();
      drawMixedPageStatic();
      unlockState();
    }
    gifPlayerRedrawOverlays(offline);
    return;
  }
  render();
}

static void setPressed(PressId id, int idx = -1) {
  if (pressedId == id && pressedIndex == idx) return;
  pressedId = id;
  pressedIndex = idx;
  redrawScreen();
}
static void clearPressed() { setPressed(PRESS_NONE, -1); }

// ── TRANSITIONS ────────────────────────────────────────────
enum TransKind { T_NONE, T_PAGE, T_SHEET, T_PUSH, T_FADE };
static TransKind tKind = T_NONE;
static Spring spring;            // T_PAGE/T_PUSH: incoming offset 0..480; T_SHEET: visible height 0..320
static bool tForward = true;     // T_PAGE: next page from the right; T_PUSH: push (true) or pop
static int tFromPage = 0;        // T_PAGE: the page to go back to if it reverts
static bool tPinStrip = false;    // T_PAGE: pin the status strip (neither end is GIF_PAGE/MOVIE_PAGE, which have none)
static bool sheetClosing = false;  // T_SHEET: false = sheet live over behindFrame; true = page live under prevFrame
static const uint16_t* fadeFrom = nullptr;
static int fadeStep = 0;
static bool tHeld = false;       // a finger owns the transition (no spring stepping)
// Diagnostics (one serial line per transition): composite frames presented
// and the slowest one -- design.md 18.4's "sheet cost" / "60 Hz" checks.
static uint32_t tFrames = 0, tMaxPresentUs = 0;
static uint32_t lastTickMs = 0;

// What was open when a sheet started to drop -- grabbing it mid-drop and
// throwing it back up restores exactly this.
static bool savedWeather = false, savedDevice = false;
static SettingsScreen savedSettings = SET_OFF;

bool navTransitionActive() { return tKind != T_NONE; }
bool navSheetOpen() { return weatherPageOpen || devicePageOpen || settingsScreen != SET_OFF; }

static const uint16_t BG = TOK_COLOR_BG_CANVAS;

static int sheetScrimLevel(int vis) {
  if (cfgReduceMotion) return 0;
  return vis < SCREEN_H / 3 ? 0 : vis < 2 * SCREEN_H / 3 ? 1 : 2;  // 100 -> 75 -> 50%
}

static void presentComposite() {
  int off = (int)lroundf(spring.x);
  switch (tKind) {
    case T_PAGE:
      // The status strip (design.md 7) is pinned so it never slides with the
      // carousel, except into/out of GIF_PAGE/MOVIE_PAGE, which have no strip
      // -- their full-screen media frame really does occupy that band.
      displayPresentSlide(prevFrame, fb(), off, tForward, shiftX, shiftY, BG,
                          tPinStrip ? TOK_LAYOUT_STRIP_Y0 : SCREEN_H);
      break;
    case T_PUSH:
      displayPresentSlide(prevFrame, fb(), off, tForward, shiftX, shiftY, BG, SCREEN_H);
      break;
    case T_SHEET:
      if (!sheetClosing) displayPresentSheet(behindFrame, fb(), off, sheetScrimLevel(off), shiftX, shiftY, BG);
      else displayPresentSheet(fb(), prevFrame, off, sheetScrimLevel(off), shiftX, shiftY, BG);
      break;
    case T_FADE:
      displayPresentFade(fadeFrom, fb(), fadeStep, shiftX, shiftY, BG);
      break;
    default:
      return;
  }
  teWaitAccumUs += displayLastTeWaitUs();
  shiftDirty = false;
  transitionDirty = false;
  tFrames++;
  uint32_t busy = displayLastPresentUs() - displayLastTeWaitUs();
  if (busy > tMaxPresentUs) tMaxPresentUs = busy;
}

static void startFade(const uint16_t* from) {
  tKind = T_FADE;
  fadeFrom = from;
  fadeStep = 1;
  transitionDirty = true;
}

static void endTransition() {
  static const char* const NAMES[] = {"none", "page", "sheet", "push", "fade"};
  if (tFrames) Serial.printf("[motion] %s: %lu frames, slowest present %luus (excl. TE wait)\n", NAMES[tKind],
                             (unsigned long)tFrames, (unsigned long)tMaxPresentUs);
  tFrames = tMaxPresentUs = 0;
  tKind = T_NONE;
  tHeld = false;
  spring.active = false;
  transitionDirty = false;
  presentFrame();
}

// ── pages ──
// Snapshot the current page, switch currentPage to `page` and compose it off
// screen. The slide itself is the spring's job.
static void beginPageTransition(int page, bool forward) {
  memcpy(prevFrame, fb(), FRAME_BYTES);
  tFromPage = currentPage;
  tPinStrip = tFromPage != GIF_PAGE && tFromPage != MOVIE_PAGE && page != GIF_PAGE && page != MOVIE_PAGE;
  currentPage = page;
  preparePage();
  tKind = T_PAGE;
  tForward = forward;
  spring.x = 0;
  spring.v = 0;
  spring.active = false;
  transitionDirty = true;
}

static void commitPageEnd() {
  // Always tracked (regardless of cfgBootPage's mode) so switching Boot Page
  // to Auto later always has a fresh page ready to resume.
  cfgLastPage = currentPage;
  queueConfigSave(CFGKEY_LAST_PAGE, currentPage);
  endTransition();
}

// Put the original page back exactly as it was (its pixels from prevFrame;
// a cat page re-blits its canvas, reopening only if the layout changed).
static void revertPage() {
  currentPage = tFromPage;
  memcpy(fb(), prevFrame, FRAME_BYTES);
  bool cat = navCatLayout();
  navSyncCatMode(cat);
  tKind = T_NONE;
  tHeld = false;
  if (cat) {
    bool held = presentHold;
    presentHold = true;
    gifPlayerRepaint(!STATE.haveData);
    presentHold = held;
  }
  presentFrame();
}

static int neighbour(bool forward) {
  return forward ? (currentPage + 1) % PAGE_COUNT : (currentPage - 1 + PAGE_COUNT) % PAGE_COUNT;
}

void navGoToPage(int page, bool forward) {
  navFinishTransition();
  if (!STATE.haveData) {
    // Offline the cats own every page, so a page change is invisible -- no
    // slide, and the cats keep playing.
    currentPage = page;
    cfgLastPage = page;
    queueConfigSave(CFGKEY_LAST_PAGE, page);
    return;
  }
  if (!ensureBuffers()) { currentPage = page; preparePage(); presentFrame(); return; }
  beginPageTransition(page, forward);
  if (cfgReduceMotion) {
    startFade(prevFrame);
    cfgLastPage = page;
    queueConfigSave(CFGKEY_LAST_PAGE, page);
    return;
  }
  springTo(spring, SCREEN_W, TOK_MOTION_SPRING_STANDARD_ZETA, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
}

// ── sheets ──
static void drawSheetInto() {
  bool held = presentHold;
  presentHold = true;
  if (settingsScreen != SET_OFF) drawSettingsScreen();
  else render();
  presentHold = held;
}

void navOpenSheet(int which) {
  navFinishTransition();
  if (!ensureBuffers()) return;
  memcpy(behindFrame, fb(), FRAME_BYTES);
  weatherPageOpen = (which == 0);
  devicePageOpen = (which == 1);
  if (which == 2) {
    settingsScreen = SET_LIST;
    settingsScrollOffset = 0;
  }
  drawSheetInto();
  if (cfgReduceMotion) { startFade(behindFrame); return; }
  tKind = T_SHEET;
  sheetClosing = false;
  spring.x = 0;
  spring.v = 0;
  springTo(spring, SCREEN_H, TOK_MOTION_SPRING_SHEET_ZETA, TOK_MOTION_SPRING_SHEET_RESPONSE_MS);
  transitionDirty = true;
}

// Swap to the dropping configuration: the sheet becomes a snapshot and the
// page under it is composed live, so it's current by the time it's revealed.
static void beginSheetDrop() {
  memcpy(prevFrame, fb(), FRAME_BYTES);
  savedWeather = weatherPageOpen;
  savedDevice = devicePageOpen;
  savedSettings = settingsScreen;
  weatherPageOpen = devicePageOpen = false;
  settingsScreen = SET_OFF;
  confirmArmedRow = -1;
  bool held = presentHold;
  presentHold = true;
  if (navCatLayout()) {
    navSyncCatMode(true);
    gifPlayerRepaint(!STATE.haveData);
  } else {
    navSyncCatMode(false);
    render();
  }
  presentHold = held;
  sheetClosing = true;
}

// Thrown back up mid-drop: the sheet is live again over the page.
static void undoSheetDrop() {
  memcpy(behindFrame, fb(), FRAME_BYTES);
  memcpy(fb(), prevFrame, FRAME_BYTES);
  weatherPageOpen = savedWeather;
  devicePageOpen = savedDevice;
  settingsScreen = savedSettings;
  sheetClosing = false;
}

static void closeSheetWith(float v) {
  if (!ensureBuffers()) return;
  float from = (tKind == T_SHEET) ? spring.x : SCREEN_H;
  if (tKind != T_SHEET || !sheetClosing) beginSheetDrop();
  if (cfgReduceMotion) { startFade(prevFrame); return; }
  tKind = T_SHEET;
  tHeld = false;
  spring.x = from;
  spring.v = v;
  bool flick = fabsf(v) > TOK_TOUCH_FLICK_BOUNCE;
  springTo(spring, 0, flick ? TOK_MOTION_SPRING_FLICK_ZETA : TOK_MOTION_SPRING_STANDARD_ZETA,
           flick ? TOK_MOTION_SPRING_FLICK_RESPONSE_MS : TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
  transitionDirty = true;
}

void navCloseSheet() {
  if (!navSheetOpen() && !(tKind == T_SHEET && sheetClosing)) return;
  if (tKind != T_SHEET) navFinishTransition();
  closeSheetWith(0);
}

// ── Settings push / pop ──
static void pushDetail() {
  if (!ensureBuffers()) { settingsScreen = SET_LEAF; renderSettings(); return; }
  memcpy(prevFrame, fb(), FRAME_BYTES);
  settingsScreen = SET_LEAF;
  drawSheetInto();
  if (cfgReduceMotion) { startFade(prevFrame); return; }
  tKind = T_PUSH;
  tForward = true;
  spring.x = 0;
  spring.v = 0;
  springTo(spring, SCREEN_W, TOK_MOTION_SPRING_STANDARD_ZETA, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
  transitionDirty = true;
}

static void beginPop() {
  memcpy(prevFrame, fb(), FRAME_BYTES);
  settingsScreen = SET_LIST;
  confirmArmedRow = -1;
  drawSheetInto();
  tKind = T_PUSH;
  tForward = false;
  spring.x = 0;
  spring.v = 0;
  spring.active = false;
  transitionDirty = true;
}

static void popDetail() {
  if (!ensureBuffers()) { settingsScreen = SET_LIST; renderSettings(); return; }
  beginPop();
  if (cfgReduceMotion) { tKind = T_NONE; startFade(prevFrame); return; }
  springTo(spring, SCREEN_W, TOK_MOTION_SPRING_STANDARD_ZETA, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
}

static void revertPop() {
  memcpy(fb(), prevFrame, FRAME_BYTES);
  settingsScreen = SET_LEAF;
  tKind = T_NONE;
  tHeld = false;
  presentFrame();
}

// Settle whatever is running at its end state, on this present.
static void settleTransition() {
  switch (tKind) {
    case T_PAGE:
      if (spring.x >= SCREEN_W - 0.5f) commitPageEnd();
      else revertPage();
      break;
    case T_SHEET:
      if (sheetClosing && spring.x <= 0.5f) {
        endTransition();
      } else if (!sheetClosing && spring.x >= SCREEN_H - 0.5f) {
        endTransition();
      } else if (sheetClosing) {
        undoSheetDrop();
        endTransition();
      } else {
        beginSheetDrop();
        endTransition();
      }
      break;
    case T_PUSH:
      if (spring.x >= SCREEN_W - 0.5f) endTransition();
      else if (!tForward) revertPop();
      else endTransition();
      break;
    case T_FADE:
      endTransition();
      break;
    default:
      break;
  }
}

void navFinishTransition() {
  if (tKind == T_NONE) return;
  if (tKind == T_FADE) { endTransition(); return; }
  spring.x = spring.target;
  spring.v = 0;
  spring.active = false;
  settleTransition();
}

// ── LIST MOMENTUM (design.md 9.4) ──────────────────────────
static bool listCoasting = false;
static float listPos = 0, listV = 0;   // scroll offset (px) and its velocity (px/s)
static Spring listSpring;              // rubber-band spring-back at an end
static const int LIST_VIEW_H = SCREEN_H - TOK_LAYOUT_HEADER_H;

static void listStop() { listCoasting = false; listSpring.active = false; }

static void listRelease(float v) {
  listPos = settingsScrollOffset;
  int maxS = settingsScrollMax();
  if (listPos < 0 || listPos > maxS) {
    listSpring.x = listPos;
    listSpring.v = v;
    springTo(listSpring, listPos < 0 ? 0 : maxS, TOK_MOTION_SPRING_STANDARD_ZETA,
             TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
    listCoasting = true;
    return;
  }
  listV = v;
  listCoasting = fabsf(v) >= 20.0f;
}

// One momentum step; returns true if the offset moved.
static bool listStep(float dt) {
  if (!listCoasting) return false;
  int maxS = settingsScrollMax();
  if (listSpring.active) {
    springStep(listSpring, dt);
    listPos = listSpring.x;
    if (!listSpring.active) listCoasting = false;
  } else {
    float decay = powf(TOK_TOUCH_DECEL, dt * 1000.0f / motionTimeScale);
    listV *= decay;
    listPos += listV * dt;
    if (listPos < 0 || listPos > maxS) {
      // Reached an end while coasting: the rest of the velocity goes to the
      // rubber-band spring.
      listSpring.x = listPos;
      listSpring.v = listV;
      springTo(listSpring, listPos < 0 ? 0 : maxS, TOK_MOTION_SPRING_STANDARD_ZETA,
               TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
    } else if (fabsf(listV) < 20.0f) {
      listCoasting = false;
    }
  }
  int off = (int)lroundf(listPos);
  if (off == settingsScrollOffset) return false;
  settingsScrollOffset = off;
  return true;
}

// ── GESTURES ───────────────────────────────────────────────
enum Target {
  TG_NONE, TG_SLEEP, TG_CLOSE, TG_SHEET_ANY, TG_ROW, TG_CELL,
  TG_WEATHER, TG_HEALTH, TG_GEAR, TG_SHUFFLE, TG_HALF
};
enum Drag { DR_NONE, DR_PAGE, DR_SHEET, DR_LIST, DR_BACK, DR_DEAD };

static struct {
  bool active = false;       // finger down on an accepted touch
  bool ignored = false;      // inside touch.rearm of the last up: dropped whole
  int32_t x0 = 0, y0 = 0, x = 0, y = 0;
  Target target = TG_NONE;
  int index = -1;
  Drag drag = DR_NONE;
  float grab = 0;            // the dragged value at the moment the drag began
  int32_t gx = 0, gy = 0;    // finger position at that moment
  bool grabbedSheet = false; // touched down on a moving sheet
} G;
static VelocityTracker vt;
static uint32_t lastUpMs = 0;
static bool touchWasDownNav = false;

static bool inRect(int32_t x, int32_t y, int x0, int y0, int x1, int y1) {
  return x >= x0 && x < x1 && y >= y0 && y < y1;
}

// Pages with the status strip: everything but the full-screen cat page (and
// the offline cats).
static bool stripPage() { return STATE.haveData && currentPage != GIF_PAGE && currentPage != MOVIE_PAGE; }

// Hit-test order (design.md 9.2): sleep corner, the modal layer, page
// controls, then the page halves.
static Target hitTest(int32_t x, int32_t y, int& idx) {
  idx = -1;
  if (inRect(x, y, SLEEP_HIT_X0, SLEEP_HIT_Y0, SLEEP_HIT_X1, SLEEP_HIT_Y1)) return TG_SLEEP;
  bool closeHit = inRect(x, y, 0, 0, TOK_CLOSE_HIT_X1, TOK_CLOSE_HIT_Y1);
  if (settingsScreen == SET_LIST) {
    if (closeHit) return TG_CLOSE;
    idx = settingsListHit(x, y);
    return idx >= 0 ? TG_ROW : TG_NONE;
  }
  if (settingsScreen == SET_LEAF) {
    if (closeHit) return TG_CLOSE;
    idx = settingsLeafHit(x, y);
    return idx >= 0 ? TG_CELL : TG_NONE;
  }
  if (weatherPageOpen || devicePageOpen) return closeHit ? TG_CLOSE : TG_SHEET_ANY;
  bool offline = !STATE.haveData;
  if (!offline && currentPage == 0 &&
      inRect(x, y, WEATHER_HIT_X0, WEATHER_HIT_Y0, WEATHER_HIT_X1, WEATHER_HIT_Y1)) return TG_WEATHER;
  if (stripPage() && y >= STRIP_HIT_Y0) {
    if (x >= HEALTH_HIT_X0 && x < HEALTH_HIT_X1) return TG_HEALTH;
    if (x >= SETTINGS_HIT_X0 && x < SETTINGS_HIT_X1) return TG_GEAR;
  }
  if (catShuffleFixed && navCatLayout()) {
    int cx, cy;
    shuffleCentre(currentPage == MIXED_PAGE && !offline, cx, cy);
    int h = SHUFFLE_HIT / 2;
    if (inRect(x, y, cx - h, cy - h, cx + h, cy + h)) return TG_SHUFFLE;
  }
  return TG_HALF;
}

static PressId pressFor(Target t) {
  switch (t) {
    case TG_SLEEP: return PRESS_SLEEP;
    case TG_CLOSE: return PRESS_CLOSE;
    case TG_ROW: return PRESS_ROW;
    case TG_CELL: return PRESS_CELL;
    case TG_WEATHER: return PRESS_WEATHER;
    case TG_HEALTH: return PRESS_HEALTH;
    case TG_GEAR: return PRESS_GEAR;
    case TG_SHUFFLE: return PRESS_SHUFFLE;
    default: return PRESS_NONE;
  }
}

static void onDown(int32_t x, int32_t y, uint32_t now) {
  G = {};
  G.active = true;
  G.x0 = G.x = x;
  G.y0 = G.y = y;
  velocityReset(vt);
  velocityAdd(vt, x, y, now);

  // A touch stops a coasting list dead, at its on-screen position.
  if (listCoasting) {
    listStop();
    settingsScrollOffset = constrain(settingsScrollOffset, 0, settingsScrollMax());
    renderSettings();
  }
  // Grabbing a moving sheet takes over from where it is (design.md 11.22).
  if (tKind == T_SHEET && !tHeld) {
    spring.active = false;
    tHeld = true;
    G.grabbedSheet = true;
    G.target = TG_NONE;
    return;
  }
  // Anything else in flight finishes on this present (the minimum
  // acceptable carousel retarget, design.md 12.3); the tap is never dropped.
  navFinishTransition();

  G.target = hitTest(x, y, G.index);
  if (G.target == TG_HALF) {
    // The lean (motion.lean): the page tilts 8px toward where it will go --
    // the page-navigation pressed state. Needs the neighbour composed now.
    if (!STATE.haveData || cfgReduceMotion || !ensureBuffers()) return;
    bool forward = x >= SWIPE_SPLIT_X;
    beginPageTransition(neighbour(forward), forward);
    tHeld = true;
    springTo(spring, TOK_MOTION_LEAN_PX, TOK_MOTION_SPRING_STANDARD_ZETA, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
    return;
  }
  PressId p = pressFor(G.target);
  if (p != PRESS_NONE) setPressed(p, G.index);
}

// The first axis past touch.slop wins; a surface that doesn't move on that
// axis just cancels the tap.
static Drag dragFor(bool horizontal, int32_t dx, int32_t dy) {
  if (G.grabbedSheet) return horizontal ? DR_DEAD : DR_SHEET;
  if (settingsScreen == SET_LIST) return horizontal ? DR_DEAD : DR_LIST;
  if (settingsScreen == SET_LEAF) {
    if (cfgReduceMotion) return DR_DEAD;
    if (horizontal) return dx > 0 ? DR_BACK : DR_DEAD;
    return DR_SHEET;
  }
  if (weatherPageOpen || devicePageOpen) return (horizontal || cfgReduceMotion) ? DR_DEAD : DR_SHEET;
  if (horizontal && STATE.haveData && !cfgReduceMotion) return DR_PAGE;
  return DR_DEAD;
}

// Horizontal page drag: offset toward the neighbour on the side the finger
// is heading, 1:1 from the grab. Crossing back past zero switches neighbour.
static void pageDragMove() {
  int32_t dx = G.x - G.gx;
  float move = tForward ? -dx : dx;
  float off = G.grab + move;
  if (off < 0) {
    // Heading the other way now: compose the other neighbour.
    int from = tFromPage;
    currentPage = from;
    memcpy(fb(), prevFrame, FRAME_BYTES);
    tForward = !tForward;
    beginPageTransition(neighbour(tForward), tForward);
    G.grab = 0;
    G.gx = G.x;
    off = 0;
  }
  spring.x = min(off, (float)SCREEN_W);
  transitionDirty = true;
}

static void startDrag(Drag d) {
  G.drag = d;
  G.gx = G.x;
  G.gy = G.y;
  if (pressedId != PRESS_NONE) clearPressed();
  switch (d) {
    case DR_PAGE: {
      bool forward = G.x < G.x0;  // finger moving left = the next page comes in
      if (tKind == T_PAGE && tForward != forward) revertPage();
      if (tKind != T_PAGE) {
        if (!ensureBuffers()) { G.drag = DR_DEAD; return; }
        beginPageTransition(neighbour(forward), forward);
      }
      tHeld = true;
      spring.active = false;
      G.grab = spring.x;
      break;
    }
    case DR_SHEET:
      if (tKind != T_SHEET) {
        tKind = T_SHEET;
        sheetClosing = false;
        spring.x = SCREEN_H;
        spring.v = 0;
      }
      tHeld = true;
      spring.active = false;
      G.grab = spring.x;
      break;
    case DR_LIST:
      G.grab = settingsScrollOffset;
      break;
    case DR_BACK:
      if (!ensureBuffers()) { G.drag = DR_DEAD; return; }
      beginPop();
      tHeld = true;
      G.grab = 0;
      break;
    default:
      break;
  }
}

static void sheetDragTo(float raw) {
  // Pulled above its open position, the sheet rubber-bands.
  if (raw > SCREEN_H) raw = SCREEN_H + rubberband(raw - SCREEN_H, SCREEN_H);
  spring.x = raw;
  transitionDirty = true;
}

static void onMove(int32_t x, int32_t y, uint32_t now) {
  G.x = x;
  G.y = y;
  velocityAdd(vt, x, y, now);
  int32_t dx = x - G.x0, dy = y - G.y0;
  if (G.drag == DR_NONE) {
    if (abs(dx) <= TOK_TOUCH_SLOP && abs(dy) <= TOK_TOUCH_SLOP) return;
    bool horizontal = abs(dx) > abs(dy);
    Drag d = dragFor(horizontal, dx, dy);
    if (d == DR_DEAD) {
      // Not a draggable axis here: the tap is cancelled (a lean springs back).
      G.drag = DR_DEAD;
      if (pressedId != PRESS_NONE) clearPressed();
      if (tKind == T_PAGE && tHeld) {
        tHeld = false;
        springTo(spring, 0, TOK_MOTION_SPRING_STANDARD_ZETA, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
      }
      return;
    }
    startDrag(d);
  }
  switch (G.drag) {
    case DR_PAGE:
      pageDragMove();
      break;
    case DR_SHEET:
      sheetDragTo(G.grab - (y - G.gy));
      break;
    case DR_LIST: {
      float raw = G.grab - (y - G.gy);
      int maxS = settingsScrollMax();
      if (raw < 0 && !cfgReduceMotion) {
        // At the top and still pulling down: the sheet itself moves (the iOS
        // rule -- the list scrolls first, then the sheet follows).
        settingsScrollOffset = 0;
        if (tKind != T_SHEET) {
          tKind = T_SHEET;
          sheetClosing = false;
          tHeld = true;
        }
        sheetDragTo(SCREEN_H + raw);
        return;
      }
      if (tKind == T_SHEET) {  // back above the top: plain list again
        tKind = T_NONE;
        tHeld = false;
        spring.x = SCREEN_H;
        presentFrame();
      }
      int off;
      if (raw < 0) off = (int)lroundf(rubberband(raw, LIST_VIEW_H));
      else if (raw > maxS) off = maxS + (int)lroundf(rubberband(raw - maxS, LIST_VIEW_H));
      else off = (int)lroundf(raw);
      if (off != settingsScrollOffset) {
        settingsScrollOffset = off;
        renderSettings();
      }
      break;
    }
    case DR_BACK:
      spring.x = constrain((float)(x - G.gx), 0.0f, (float)SCREEN_W);
      transitionDirty = true;
      break;
    default:
      break;
  }
}

// Release a held sheet: fast = the velocity's direction decides; slow = the
// projected position vs half the height. Then a spring with the velocity.
static void releaseSheet(float vy) {
  float vOpen = -vy;  // px/s toward open
  bool open;
  if (fabsf(vOpen) > TOK_TOUCH_FLICK_COMMIT) open = vOpen > 0;
  else open = spring.x + projectDistance(vOpen) > SCREEN_H / 2;
  tHeld = false;
  if (open) {
    if (sheetClosing) undoSheetDrop();
    spring.v = vOpen;
    bool flick = fabsf(vOpen) > TOK_TOUCH_FLICK_BOUNCE;
    springTo(spring, SCREEN_H, flick ? TOK_MOTION_SPRING_FLICK_ZETA : TOK_MOTION_SPRING_STANDARD_ZETA,
             flick ? TOK_MOTION_SPRING_FLICK_RESPONSE_MS : TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
    transitionDirty = true;
  } else {
    closeSheetWith(vOpen);
  }
}

// Release a held horizontal transition (page swipe or swipe back).
static void releaseSlide(float vToward) {
  bool commit;
  if (fabsf(vToward) > TOK_TOUCH_FLICK_COMMIT) commit = vToward > 0;
  else commit = spring.x + projectDistance(vToward) > SCREEN_W / 2;
  tHeld = false;
  spring.v = vToward;
  // Pages never overshoot (no third frame to show past the edge): zeta 1.0.
  springTo(spring, commit ? SCREEN_W : 0, TOK_MOTION_SPRING_STANDARD_ZETA, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
  transitionDirty = true;
}

static void commitTap(uint32_t now) {
  Target t = G.target;
  int idx = G.index;
  pressedId = PRESS_NONE;
  pressedIndex = -1;
  switch (t) {
    case TG_SLEEP:
      redrawScreen();
      enterScreenSleep();
      break;
    case TG_CLOSE:
      if (settingsScreen == SET_LEAF) popDetail();
      else navCloseSheet();
      break;
    case TG_SHEET_ANY:
      navCloseSheet();  // read-only sheets: tap anywhere dismisses
      break;
    case TG_ROW:
      if (settingsListActivate(idx)) pushDetail();
      else renderSettings();
      break;
    case TG_CELL:
      settingsLeafActivate(idx, now);
      renderSettings();
      break;
    case TG_WEATHER: redrawScreen(); navOpenSheet(0); break;
    case TG_HEALTH: redrawScreen(); navOpenSheet(1); break;
    case TG_GEAR: redrawScreen(); navOpenSheet(2); break;
    case TG_SHUFFLE:
      gifPlayerResetForPageChange();  // the next tick opens (and draws) a new cat
      break;
    case TG_HALF:
      if (tKind == T_PAGE) {
        tHeld = false;
        springTo(spring, SCREEN_W, TOK_MOTION_SPRING_STANDARD_ZETA, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
      } else {
        navGoToPage(neighbour(G.x0 >= SWIPE_SPLIT_X), G.x0 >= SWIPE_SPLIT_X);
      }
      break;
    default:
      break;
  }
}

static void onUp(uint32_t now) {
  float vx, vy;
  velocityGet(vt, now, vx, vy);
  G.active = false;
  switch (G.drag) {
    case DR_NONE:
      if (G.grabbedSheet) releaseSheet(0);
      else commitTap(now);
      break;
    case DR_DEAD:
      if (G.grabbedSheet) releaseSheet(0);
      if (tKind == T_PAGE && tHeld) {
        tHeld = false;
        springTo(spring, 0, TOK_MOTION_SPRING_STANDARD_ZETA, TOK_MOTION_SPRING_STANDARD_RESPONSE_MS);
      }
      break;
    case DR_PAGE:
      releaseSlide(tForward ? -vx : vx);
      break;
    case DR_BACK:
      releaseSlide(vx);
      break;
    case DR_SHEET:
      releaseSheet(vy);
      break;
    case DR_LIST:
      if (tKind == T_SHEET) releaseSheet(vy);
      else listRelease(-vy);
      break;
  }
  G.drag = DR_NONE;
}

void navTouch(bool down, int32_t x, int32_t y, uint32_t now) {
  if (down && !touchWasDownNav) {
    Serial.printf("[touch] down x=%ld y=%ld\n", (long)x, (long)y);
    // touch.rearm: a down within 120 ms of the last up is controller bounce.
    G.ignored = (now - lastUpMs < TOK_TOUCH_REARM_MS);
    if (!G.ignored) onDown(x, y, now);
  } else if (down && touchWasDownNav) {
    if (G.active && !G.ignored && (x != G.x || y != G.y)) onMove(x, y, now);
  } else if (!down && touchWasDownNav) {
    Serial.printf("[touch] up\n");
    if (G.active && !G.ignored) onUp(now);
    lastUpMs = now;
  }
  touchWasDownNav = down;
}

// Drop any gesture in progress (screen sleep): no commit, no drag.
void navResetGesture() {
  G = {};
  touchWasDownNav = false;
  pressedId = PRESS_NONE;
  pressedIndex = -1;
}

bool navTick(uint32_t now) {
  float dt = lastTickMs ? (now - lastTickMs) / 1000.0f : 0.033f;
  lastTickMs = now;
  bool presented = false;

  if (listCoasting && settingsScreen == SET_LIST && tKind == T_NONE) {
    if (listStep(dt)) { renderSettings(); presented = true; }
  } else if (listCoasting) {
    listStop();
  }

  if (tKind == T_NONE) return presented;
  if (tKind == T_FADE) {
    if (fadeStep > TOK_MOTION_FADE_FRAMES) { endTransition(); return true; }
    presentComposite();
    fadeStep++;
    return true;
  }
  bool moving = false;
  if (!tHeld) moving = springStep(spring, dt);
  if (moving || transitionDirty) {
    presentComposite();
    presented = true;
  }
  if (!tHeld && !spring.active) settleTransition();
  return presented;
}
