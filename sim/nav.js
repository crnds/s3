// sim/nav.js -- touch router, gestures and transitions (src/nav.cpp).

// ── NAV (nav.cpp twin) ────────────────────────────────────────
const NAV = {
  pressedId: null, pressedIndex: -1,
  tKind: null,           // null | 'page' | 'sheet' | 'push' | 'fade'
  spring: makeSpring(), tForward: true, tFromPage: 0, tPinStrip: false, sheetClosing: false,
  fadeFrom: null, fadeStep: 0, tHeld: false, lastTickMs: 0,
  saved: null,
  listCoasting: false, listPos: 0, listV: 0, listSpring: makeSpring(),
  G: null, vt: makeVelocity(), lastUpMs: -1e9, wasDown: false,
};
const LIST_VIEW_H = SCREEN_H - TOK_LAYOUT_HEADER_H;
function isCatPage(p) { return p === GIF_PAGE || p === MOVIE_PAGE || p === MIXED_PAGE || p === OVERVIEW_PAGE; }
function navCatLayout() { return isCatPage(STATE.currentPage) || !STATE.haveData; }
function navTransitionActive() { return NAV.tKind !== null; }
function navSheetOpen() { return STATE.weatherPageOpen || STATE.devicePageOpen || STATE.settingsScreen !== 'off'; }
function withHold(fn) { const h = presentHold; presentHold = true; fn(); presentHold = h; }
function preparePage() { withHold(render); }
function redrawScreen() {
  if (STATE.settingsScreen !== 'off') renderSettings(); else render();
}
function setPressed(id, idx = -1) {
  if (NAV.pressedId === id && NAV.pressedIndex === idx) return;
  NAV.pressedId = id; NAV.pressedIndex = idx;
  redrawScreen();
}
function clearPressed() { setPressed(null, -1); }
function sheetScrimLevel(vis) {
  if (STATE.cfgReduceMotion) return 0;
  return vis < SCREEN_H / 3 ? 0 : vis < 2 * SCREEN_H / 3 ? 1 : 2;
}
function presentComposite() {
  const off = Math.round(NAV.spring.x);
  switch (NAV.tKind) {
    case 'page': presentSlide(prevFrame, frameCanvas, off, NAV.tForward, NAV.tPinStrip ? TOK_LAYOUT_STRIP_Y0 : SCREEN_H); break;
    case 'push': presentSlide(prevFrame, frameCanvas, off, NAV.tForward); break;
    case 'sheet':
      if (!NAV.sheetClosing) presentSheet(behindFrame, frameCanvas, off, sheetScrimLevel(off));
      else presentSheet(frameCanvas, prevFrame, off, sheetScrimLevel(off));
      break;
    case 'fade': presentFade(NAV.fadeFrom, frameCanvas, NAV.fadeStep); break;
    default: return;
  }
  STATE.shiftDirty = false;
  transitionDirty = false;
}
function startFade(from) { NAV.tKind = 'fade'; NAV.fadeFrom = from; NAV.fadeStep = 1; transitionDirty = true; }
function endTransition() {
  NAV.tKind = null; NAV.tHeld = false; NAV.spring.active = false; transitionDirty = false;
  presentFrame();
}
function saveLastPage() { STATE.cfgLastPage = STATE.currentPage; lsSet(LAST_PAGE_KEY, String(STATE.currentPage)); }
function beginPageTransition(page, forward) {
  copyFrame(prevFrame, frameCanvas);
  NAV.tFromPage = STATE.currentPage;
  NAV.tPinStrip = NAV.tFromPage !== GIF_PAGE && NAV.tFromPage !== MOVIE_PAGE && page !== GIF_PAGE && page !== MOVIE_PAGE;
  STATE.currentPage = page;
  preparePage();
  NAV.tKind = 'page'; NAV.tForward = forward;
  Object.assign(NAV.spring, { x: 0, v: 0, active: false });
  transitionDirty = true;
}
function commitPageEnd() { saveLastPage(); endTransition(); }
function revertPage() {
  STATE.currentPage = NAV.tFromPage;
  copyFrame(frameCanvas, prevFrame);
  NAV.tKind = null; NAV.tHeld = false;
  presentFrame();
}
function neighbour(forward) { return forward ? (STATE.currentPage + 1) % PAGE_COUNT : (STATE.currentPage - 1 + PAGE_COUNT) % PAGE_COUNT; }
function navGoToPage(page, forward) {
  navFinishTransition();
  if (!STATE.haveData) { STATE.currentPage = page; saveLastPage(); return; }
  beginPageTransition(page, forward);
  if (STATE.cfgReduceMotion) { startFade(prevFrame); saveLastPage(); return; }
  springTo(NAV.spring, SCREEN_W, TOK_MOTION_SPRING_STANDARD);
}
function drawSheetInto() { withHold(() => { if (STATE.settingsScreen !== 'off') drawSettingsScreen(); else render(); }); }
function navOpenSheet(which) {
  navFinishTransition();
  copyFrame(behindFrame, frameCanvas);
  STATE.weatherPageOpen = which === 0;
  STATE.devicePageOpen = which === 1;
  if (which === 2) { STATE.settingsScreen = 'list'; STATE.settingsSection = -1; STATE.settingsScrollOffset = 0; }
  drawSheetInto();
  if (STATE.cfgReduceMotion) { startFade(behindFrame); return; }
  NAV.tKind = 'sheet'; NAV.sheetClosing = false;
  Object.assign(NAV.spring, { x: 0, v: 0 });
  springTo(NAV.spring, SCREEN_H, TOK_MOTION_SPRING_SHEET);
  transitionDirty = true;
}
// Theme change under an open sheet: recompose the page into behindFrame (the
// firmware's navRefreshBehind); the sheet's own pixels are parked in prevFrame.
function navRefreshBehind() {
  if (navTransitionActive() || !navSheetOpen()) return;
  copyFrame(prevFrame, frameCanvas);
  const w = STATE.weatherPageOpen, d = STATE.devicePageOpen, s = STATE.settingsScreen;
  STATE.weatherPageOpen = STATE.devicePageOpen = false;
  STATE.settingsScreen = 'off';
  withHold(render);
  copyFrame(behindFrame, frameCanvas);
  copyFrame(frameCanvas, prevFrame);
  STATE.weatherPageOpen = w; STATE.devicePageOpen = d; STATE.settingsScreen = s;
}
function beginSheetDrop() {
  copyFrame(prevFrame, frameCanvas);
  NAV.saved = { w: STATE.weatherPageOpen, d: STATE.devicePageOpen, s: STATE.settingsScreen };
  STATE.weatherPageOpen = STATE.devicePageOpen = false;
  STATE.settingsScreen = 'off';
  STATE.confirmArmedRow = -1;
  withHold(render);
  NAV.sheetClosing = true;
}
function undoSheetDrop() {
  copyFrame(behindFrame, frameCanvas);
  copyFrame(frameCanvas, prevFrame);
  STATE.weatherPageOpen = NAV.saved.w; STATE.devicePageOpen = NAV.saved.d; STATE.settingsScreen = NAV.saved.s;
  NAV.sheetClosing = false;
}
function closeSheetWith(v) {
  const from = NAV.tKind === 'sheet' ? NAV.spring.x : SCREEN_H;
  if (NAV.tKind !== 'sheet' || !NAV.sheetClosing) beginSheetDrop();
  if (STATE.cfgReduceMotion) { startFade(prevFrame); return; }
  NAV.tKind = 'sheet'; NAV.tHeld = false;
  NAV.spring.x = from; NAV.spring.v = v;
  springTo(NAV.spring, 0, Math.abs(v) > TOK_TOUCH_FLICK_BOUNCE ? TOK_MOTION_SPRING_FLICK : TOK_MOTION_SPRING_STANDARD);
  transitionDirty = true;
}
function navCloseSheet() {
  if (!navSheetOpen() && !(NAV.tKind === 'sheet' && NAV.sheetClosing)) return;
  if (NAV.tKind !== 'sheet') navFinishTransition();
  closeSheetWith(0);
}
// Push the row just activated: a leaf detail, or (section) its child list.
function pushDetail(section) {
  copyFrame(prevFrame, frameCanvas);
  if (section) settingsEnterSection(); else STATE.settingsScreen = 'leaf';
  drawSheetInto();
  if (STATE.cfgReduceMotion) { startFade(prevFrame); return; }
  NAV.tKind = 'push'; NAV.tForward = true;
  Object.assign(NAV.spring, { x: 0, v: 0 });
  springTo(NAV.spring, SCREEN_W, TOK_MOTION_SPRING_STANDARD);
  transitionDirty = true;
}
let popFromSection = false;  // the pop leaves a section's list (else a leaf)
function beginPop() {
  copyFrame(prevFrame, frameCanvas);
  popFromSection = STATE.settingsScreen === 'list';
  if (popFromSection) settingsLeaveSection();
  STATE.settingsScreen = 'list';
  STATE.confirmArmedRow = -1;
  drawSheetInto();
  NAV.tKind = 'push'; NAV.tForward = false;
  Object.assign(NAV.spring, { x: 0, v: 0, active: false });
  transitionDirty = true;
}
function popDetail() {
  beginPop();
  if (STATE.cfgReduceMotion) { NAV.tKind = null; startFade(prevFrame); return; }
  springTo(NAV.spring, SCREEN_W, TOK_MOTION_SPRING_STANDARD);
}
function revertPop() {
  copyFrame(frameCanvas, prevFrame);
  if (popFromSection) settingsUndoLeaveSection();
  STATE.settingsScreen = popFromSection ? 'list' : 'leaf';
  NAV.tKind = null; NAV.tHeld = false;
  presentFrame();
}
function settleTransition() {
  const x = NAV.spring.x;
  switch (NAV.tKind) {
    case 'page': if (x >= SCREEN_W - 0.5) commitPageEnd(); else revertPage(); break;
    case 'sheet':
      if (NAV.sheetClosing && x <= 0.5) endTransition();
      else if (!NAV.sheetClosing && x >= SCREEN_H - 0.5) endTransition();
      else if (NAV.sheetClosing) { undoSheetDrop(); endTransition(); }
      else { beginSheetDrop(); endTransition(); }
      break;
    case 'push':
      if (x >= SCREEN_W - 0.5) endTransition();
      else if (!NAV.tForward) revertPop();
      else endTransition();
      break;
    case 'fade': endTransition(); break;
  }
}
function navFinishTransition() {
  if (!NAV.tKind) return;
  if (NAV.tKind === 'fade') { endTransition(); return; }
  Object.assign(NAV.spring, { x: NAV.spring.target, v: 0, active: false });
  settleTransition();
}

// List momentum.
function listStop() { NAV.listCoasting = false; NAV.listSpring.active = false; }
function listRelease(v) {
  NAV.listPos = STATE.settingsScrollOffset;
  const maxS = settingsScrollMax();
  if (NAV.listPos < 0 || NAV.listPos > maxS) {
    Object.assign(NAV.listSpring, { x: NAV.listPos, v });
    springTo(NAV.listSpring, NAV.listPos < 0 ? 0 : maxS, TOK_MOTION_SPRING_STANDARD);
    NAV.listCoasting = true;
    return;
  }
  NAV.listV = v;
  NAV.listCoasting = Math.abs(v) >= 20;
}
function listStep(dt) {
  if (!NAV.listCoasting) return false;
  const maxS = settingsScrollMax();
  if (NAV.listSpring.active) {
    springStep(NAV.listSpring, dt);
    NAV.listPos = NAV.listSpring.x;
    if (!NAV.listSpring.active) NAV.listCoasting = false;
  } else {
    NAV.listV *= Math.pow(TOK_TOUCH_DECEL, dt * 1000 / motionTimeScale);
    NAV.listPos += NAV.listV * dt;
    if (NAV.listPos < 0 || NAV.listPos > maxS) {
      Object.assign(NAV.listSpring, { x: NAV.listPos, v: NAV.listV });
      springTo(NAV.listSpring, NAV.listPos < 0 ? 0 : maxS, TOK_MOTION_SPRING_STANDARD);
    } else if (Math.abs(NAV.listV) < 20) NAV.listCoasting = false;
  }
  const off = Math.round(NAV.listPos);
  if (off === STATE.settingsScrollOffset) return false;
  STATE.settingsScrollOffset = off;
  return true;
}

// ── GESTURES (nav.cpp twin) ───────────────────────────────────
function inRect(x, y, x0, y0, x1, y1) { return x >= x0 && x < x1 && y >= y0 && y < y1; }
function stripPage() { return STATE.haveData && STATE.currentPage !== GIF_PAGE && STATE.currentPage !== MOVIE_PAGE; }
function hitTest(x, y) {
  if (inRect(x, y, SLEEP_HIT_X0, SLEEP_HIT_Y0, SLEEP_HIT_X1, SLEEP_HIT_Y1)) return ['sleep', -1];
  const closeHit = inRect(x, y, 0, 0, TOK_CLOSE_HIT_X1, TOK_CLOSE_HIT_Y1);
  if (STATE.settingsScreen === 'list') {
    if (closeHit) return ['close', -1];
    const i = settingsListHit(x, y);
    return i >= 0 ? ['row', i] : [null, -1];
  }
  if (STATE.settingsScreen === 'leaf') {
    if (closeHit) return ['close', -1];
    const i = settingsLeafHit(x, y);
    return i >= 0 ? ['cell', i] : [null, -1];
  }
  if (STATE.weatherPageOpen) return ['sheetAny', -1];  // no close glyph: tap anywhere
  if (STATE.devicePageOpen) return [closeHit ? 'close' : 'sheetAny', -1];
  const offline = !STATE.haveData;
  // Page controls (design.md 9.2): shuffle, then the 56x40 strip targets, then
  // the Weather card, which gives up its bottom corner to the gear.
  if (STATE.catShuffleSec < 0 && navCatLayout()) {
    const [cx, cy] = shuffleCentre(offline);
    const h = SHUFFLE_HIT / 2;
    if (inRect(x, y, cx - h, cy - h, cx + h, cy + h)) return ['shuffle', -1];
  }
  if (stripPage() && y >= STRIP_HIT_Y0) {
    if (x >= HEALTH_HIT_X0 && x < HEALTH_HIT_X1) return ['health', -1];
    if (x >= SETTINGS_HIT_X0 && x < SETTINGS_HIT_X1) return ['gear', -1];
  }
  if (!offline && STATE.currentPage === 0 && inRect(x, y, WEATHER_HIT_X0, WEATHER_HIT_Y0, WEATHER_HIT_X1, WEATHER_HIT_Y1)) return ['weather', -1];
  if (!offline && STATE.currentPage === OVERVIEW_PAGE && inRect(x, y, OV_WX_HIT_X0, OV_WX_HIT_Y0, OV_WX_HIT_X1, OV_WX_HIT_Y1)) return ['weather', -1];
  return ['half', -1];
}
const PRESSABLE = new Set(['sleep', 'close', 'row', 'cell', 'weather', 'health', 'gear', 'shuffle']);

function onDown(x, y, now) {
  const G = NAV.G = { active: true, x0: x, y0: y, x, y, target: null, index: -1, drag: null, grab: 0, gx: 0, gy: 0, grabbedSheet: false };
  NAV.vt = makeVelocity();
  velocityAdd(NAV.vt, x, y, now);
  if (NAV.listCoasting) {
    listStop();
    STATE.settingsScrollOffset = Math.max(0, Math.min(settingsScrollMax(), STATE.settingsScrollOffset));
    renderSettings();
  }
  if (NAV.tKind === 'sheet' && !NAV.tHeld) {
    NAV.spring.active = false; NAV.tHeld = true; G.grabbedSheet = true;
    return;
  }
  navFinishTransition();
  [G.target, G.index] = hitTest(x, y);
  if (G.target === 'half') {
    if (!STATE.haveData || STATE.cfgReduceMotion) return;
    const forward = x >= SWIPE_SPLIT_X;
    beginPageTransition(neighbour(forward), forward);
    NAV.tHeld = true;
    springTo(NAV.spring, TOK_MOTION_LEAN_PX, TOK_MOTION_SPRING_STANDARD);
    return;
  }
  if (PRESSABLE.has(G.target)) setPressed(G.target, G.index);
}
function dragFor(horizontal, dx) {
  const G = NAV.G;
  if (G.grabbedSheet) return horizontal ? 'dead' : 'sheet';
  if (STATE.settingsScreen === 'list') {
    if (!horizontal) return 'list';
    // A section's list swipes back to the main list like a detail does.
    return (STATE.settingsSection >= 0 && dx > 0 && !STATE.cfgReduceMotion) ? 'back' : 'dead';
  }
  if (STATE.settingsScreen === 'leaf') {
    if (STATE.cfgReduceMotion) return 'dead';
    if (horizontal) return dx > 0 ? 'back' : 'dead';
    return 'sheet';
  }
  if (STATE.weatherPageOpen || STATE.devicePageOpen) return (horizontal || STATE.cfgReduceMotion) ? 'dead' : 'sheet';
  if (horizontal && STATE.haveData && !STATE.cfgReduceMotion) return 'page';
  return 'dead';
}
function pageDragMove() {
  const G = NAV.G;
  const dx = G.x - G.gx;
  let off = G.grab + (NAV.tForward ? -dx : dx);
  if (off < 0) {
    STATE.currentPage = NAV.tFromPage;
    copyFrame(frameCanvas, prevFrame);
    NAV.tForward = !NAV.tForward;
    beginPageTransition(neighbour(NAV.tForward), NAV.tForward);
    G.grab = 0; G.gx = G.x; off = 0;
  }
  NAV.spring.x = Math.min(off, SCREEN_W);
  transitionDirty = true;
}
function startDrag(d) {
  const G = NAV.G;
  G.drag = d; G.gx = G.x; G.gy = G.y;
  if (NAV.pressedId) clearPressed();
  if (d === 'page') {
    const forward = G.x < G.x0;
    if (NAV.tKind === 'page' && NAV.tForward !== forward) revertPage();
    if (NAV.tKind !== 'page') beginPageTransition(neighbour(forward), forward);
    NAV.tHeld = true; NAV.spring.active = false; G.grab = NAV.spring.x;
  } else if (d === 'sheet') {
    if (NAV.tKind !== 'sheet') { NAV.tKind = 'sheet'; NAV.sheetClosing = false; NAV.spring.x = SCREEN_H; NAV.spring.v = 0; }
    NAV.tHeld = true; NAV.spring.active = false; G.grab = NAV.spring.x;
  } else if (d === 'list') {
    G.grab = STATE.settingsScrollOffset;
  } else if (d === 'back') {
    beginPop(); NAV.tHeld = true; G.grab = 0;
  }
}
function sheetDragTo(raw) {
  if (raw > SCREEN_H) raw = SCREEN_H + rubberband(raw - SCREEN_H, SCREEN_H);
  NAV.spring.x = raw;
  transitionDirty = true;
}
function onMove(x, y, now) {
  const G = NAV.G;
  G.x = x; G.y = y;
  velocityAdd(NAV.vt, x, y, now);
  const dx = x - G.x0, dy = y - G.y0;
  if (!G.drag) {
    if (Math.abs(dx) <= TOK_TOUCH_SLOP && Math.abs(dy) <= TOK_TOUCH_SLOP) return;
    const d = dragFor(Math.abs(dx) > Math.abs(dy), dx);
    if (d === 'dead') {
      G.drag = 'dead';
      if (NAV.pressedId) clearPressed();
      if (NAV.tKind === 'page' && NAV.tHeld) { NAV.tHeld = false; springTo(NAV.spring, 0, TOK_MOTION_SPRING_STANDARD); }
      return;
    }
    startDrag(d);
  }
  switch (G.drag) {
    case 'page': pageDragMove(); break;
    case 'sheet': sheetDragTo(G.grab - (y - G.gy)); break;
    case 'list': {
      const raw = G.grab - (y - G.gy);
      const maxS = settingsScrollMax();
      if (raw < 0 && !STATE.cfgReduceMotion) {
        STATE.settingsScrollOffset = 0;
        if (NAV.tKind !== 'sheet') { NAV.tKind = 'sheet'; NAV.sheetClosing = false; NAV.tHeld = true; }
        sheetDragTo(SCREEN_H + raw);
        return;
      }
      if (NAV.tKind === 'sheet') { NAV.tKind = null; NAV.tHeld = false; NAV.spring.x = SCREEN_H; presentFrame(); }
      let off;
      if (raw < 0) off = Math.round(rubberband(raw, LIST_VIEW_H));
      else if (raw > maxS) off = maxS + Math.round(rubberband(raw - maxS, LIST_VIEW_H));
      else off = Math.round(raw);
      if (off !== STATE.settingsScrollOffset) { STATE.settingsScrollOffset = off; renderSettings(); }
      break;
    }
    case 'back':
      NAV.spring.x = Math.max(0, Math.min(SCREEN_W, x - G.gx));
      transitionDirty = true;
      break;
  }
}
function releaseSheet(vy) {
  const vOpen = -vy;
  const open = Math.abs(vOpen) > TOK_TOUCH_FLICK_COMMIT ? vOpen > 0 : NAV.spring.x + projectDistance(vOpen) > SCREEN_H / 2;
  NAV.tHeld = false;
  if (open) {
    if (NAV.sheetClosing) undoSheetDrop();
    NAV.spring.v = vOpen;
    springTo(NAV.spring, SCREEN_H, Math.abs(vOpen) > TOK_TOUCH_FLICK_BOUNCE ? TOK_MOTION_SPRING_FLICK : TOK_MOTION_SPRING_STANDARD);
    transitionDirty = true;
  } else {
    closeSheetWith(vOpen);
  }
}
function releaseSlide(v) {
  const commit = Math.abs(v) > TOK_TOUCH_FLICK_COMMIT ? v > 0 : NAV.spring.x + projectDistance(v) > SCREEN_W / 2;
  NAV.tHeld = false;
  NAV.spring.v = v;
  springTo(NAV.spring, commit ? SCREEN_W : 0, TOK_MOTION_SPRING_STANDARD);
  transitionDirty = true;
}
function commitTap(now) {
  const { target, index } = NAV.G;
  NAV.pressedId = null; NAV.pressedIndex = -1;
  switch (target) {
    case 'sleep': redrawScreen(); enterScreenSleep(); break;
    case 'close': if (STATE.settingsScreen === 'leaf' || (STATE.settingsScreen === 'list' && STATE.settingsSection >= 0)) popDetail(); else navCloseSheet(); break;
    case 'sheetAny': navCloseSheet(); break;
    case 'row': {
      const go = settingsListActivate(index);
      if (go === 'leaf') pushDetail(false); else if (go === 'section') pushDetail(true); else renderSettings();
      break;
    }
    case 'cell': settingsLeafActivate(index, now); renderSettings(); break;
    case 'weather': redrawScreen(); navOpenSheet(0); break;
    case 'health': redrawScreen(); navOpenSheet(1); break;
    case 'gear': redrawScreen(); navOpenSheet(2); break;
    case 'shuffle':
      console.log(STATE.currentPage === MOVIE_PAGE
        ? '[sim] shuffle: the device opens a new random movie here.'
        : '[sim] shuffle: the device opens a new random cat here.');
      redrawScreen();
      break;
    case 'half':
      if (NAV.tKind === 'page') { NAV.tHeld = false; springTo(NAV.spring, SCREEN_W, TOK_MOTION_SPRING_STANDARD); }
      else { const f = NAV.G.x0 >= SWIPE_SPLIT_X; navGoToPage(neighbour(f), f); }
      break;
  }
}
function onUp(now) {
  const G = NAV.G;
  const [vx, vy] = velocityGet(NAV.vt, now);
  G.active = false;
  switch (G.drag) {
    case null: if (G.grabbedSheet) releaseSheet(0); else commitTap(now); break;
    case 'dead':
      if (G.grabbedSheet) releaseSheet(0);
      if (NAV.tKind === 'page' && NAV.tHeld) { NAV.tHeld = false; springTo(NAV.spring, 0, TOK_MOTION_SPRING_STANDARD); }
      break;
    case 'page': releaseSlide(NAV.tForward ? -vx : vx); break;
    case 'back': releaseSlide(vx); break;
    case 'sheet': releaseSheet(vy); break;
    case 'list': if (NAV.tKind === 'sheet') releaseSheet(vy); else listRelease(-vy); break;
  }
  G.drag = null;
}
function navTouch(down, x, y, now) {
  const G = NAV.G;
  if (down && !NAV.wasDown) {
    NAV.ignored = now - NAV.lastUpMs < TOK_TOUCH_REARM_MS;  // touch.rearm
    if (!NAV.ignored) onDown(x, y, now);
  } else if (down && NAV.wasDown) {
    if (G && G.active && !NAV.ignored && (x !== G.x || y !== G.y)) onMove(x, y, now);
  } else if (!down && NAV.wasDown) {
    if (G && G.active && !NAV.ignored) onUp(now);
    NAV.lastUpMs = now;
  }
  NAV.wasDown = down;
}
function navTick(now) {
  const dt = NAV.lastTickMs ? (now - NAV.lastTickMs) / 1000 : 0.033;
  NAV.lastTickMs = now;
  let presented = false;
  if (NAV.listCoasting && STATE.settingsScreen === 'list' && !NAV.tKind) {
    if (listStep(dt)) { renderSettings(); presented = true; }
  } else if (NAV.listCoasting) listStop();
  if (!NAV.tKind) return presented;
  if (NAV.tKind === 'fade') {
    if (NAV.fadeStep > TOK_MOTION_FADE_FRAMES) { endTransition(); return true; }
    presentComposite();
    NAV.fadeStep++;
    return true;
  }
  let moving = false;
  if (!NAV.tHeld) moving = springStep(NAV.spring, dt);
  if (moving || transitionDirty) { presentComposite(); presented = true; }
  if (!NAV.tHeld && !NAV.spring.active) settleTransition();
  return presented;
}
