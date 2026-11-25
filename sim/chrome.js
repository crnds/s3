// sim/chrome.js -- sim-only: the page around the screen (controls, log) and pointer/key input.

// Where polls get their data (the Connection card's Data menu): 'auto' tries
// the server and falls back to mock data, so the simulator runs with no
// server at all; 'server' shows the real offline path; 'mock' never fetches.
// ?data=auto|server|mock overrides the remembered choice.
const DATA_SOURCE_KEY = 's3dashboard_v1_datasource';
const DATA_SOURCES = ['auto', 'server', 'mock'];
function initDataSource() {
  const q = new URLSearchParams(location.search).get('data');
  const v = DATA_SOURCES.includes(q) ? q : lsGet(DATA_SOURCE_KEY);
  if (DATA_SOURCES.includes(v)) document.getElementById('data-source').value = v;
}
function dataSource() { return document.getElementById('data-source').value; }

// ── UI WIRING ─────────────────────────────────────────────────
function setConn(ok, detail) {
  document.getElementById('conn-status').innerHTML =
    `<span class="dot ${ok ? 'ok' : 'bad'}"></span>${ok ? 'connected' : 'disconnected'}`;
  document.getElementById('fetch-status').textContent = detail;
}
function log(msg, cls) {
  const el = document.getElementById('log');
  const line = document.createElement('div');
  if (cls) line.className = cls;
  line.textContent = new Date().toLocaleTimeString() + '  ' + msg;
  el.prepend(line);
  while (el.childElementCount > 50) el.removeChild(el.lastChild);
}
function setPageStatus(t) { document.getElementById('page-status').textContent = t; }
function describeScreen() {
  if (STATE.screenSleeping) return 'SLEEP (tap to wake)';
  if (STATE.settingsScreen === 'leaf') return 'SETTINGS: ' + rows()[STATE.settingsLeafIndex].label;
  if (STATE.settingsScreen === 'list') return STATE.settingsSection >= 0 ? 'SETTINGS: ' + SETTINGS[STATE.settingsSection].label : 'SETTINGS (list)';
  if (STATE.weatherPageOpen) return 'WEATHER (tap to close)';
  if (STATE.devicePageOpen) return 'DEVICE STATS (tap to close)';
  if (!STATE.haveData) return 'OFFLINE (cat GIFs)';
  const names = ['status', 'usage', 'cats', 'movies', 'status + cats', 'note', 'overview'];
  let s = `page ${STATE.currentPage + 1}/${PAGE_COUNT} (${names[STATE.currentPage]})`;
  if (STATE.shiftX || STATE.shiftY) s += ` · shift (${STATE.shiftX},${STATE.shiftY})`;
  if (motionTimeScale > 1) s += ` · slow motion x${motionTimeScale}`;
  return s;
}

// ── POINTER INPUT ─────────────────────────────────────────────
// Pointer events are queued and fed to navTouch() from the loop, the way
// the firmware samples the touch controller once per pass.
const pointerQueue = [];
let pointerDown = false;
function screenXY(event) {
  const r = canvas.getBoundingClientRect();
  return [Math.floor((event.clientX - r.left - 3) * SCREEN_W / (r.width - 6)),
          Math.floor((event.clientY - r.top - 3) * SCREEN_H / (r.height - 6))];
}
canvas.addEventListener('pointerdown', (event) => {
  const [x, y] = screenXY(event);
  pointerDown = true;
  canvas.setPointerCapture(event.pointerId);
  pointerQueue.push({ down: true, x, y, t: performance.now() });
});
canvas.addEventListener('pointermove', (event) => {
  if (!pointerDown) return;
  const [x, y] = screenXY(event);
  pointerQueue.push({ down: true, x, y, t: performance.now() });
});
function pointerUp(event) {
  if (!pointerDown) return;
  pointerDown = false;
  const [x, y] = screenXY(event);
  pointerQueue.push({ down: false, x, y, t: performance.now() });
}
canvas.addEventListener('pointerup', pointerUp);
canvas.addEventListener('pointercancel', pointerUp);
let touchWasDown = false;
function processTouches() {
  while (pointerQueue.length) {
    const e = pointerQueue.shift();
    if (STATE.screenSleeping) {
      // The waking press is swallowed -- it never reaches the screen.
      if (e.down && !touchWasDown && e.t - STATE.sleepStartMs > TOK_TOUCH_WAKE_GUARD_MS) {
        swallowUntilUp = true;
        exitScreenSleep();
      }
    } else if (swallowUntilUp) {
      if (!e.down) swallowUntilUp = false;
    } else {
      navTouch(e.down, e.x, e.y, e.t);
    }
    touchWasDown = e.down;
  }
}

// Keyboard: the same single-key commands the firmware takes over serial.
function closeSheetNow() { if (navSheetOpen()) navCloseSheet(); navFinishTransition(); }
canvas.addEventListener('keydown', (event) => {
  const k = event.key;
  if (k === 'z') {
    event.preventDefault();
    if (STATE.screenSleeping) exitScreenSleep(); else enterScreenSleep();
    return;
  }
  if (STATE.screenSleeping) return;
  if (k === 'h') hourlyTestStartMs = performance.now();
  else if (k === 'n' || k === 'ArrowRight') { closeSheetNow(); navGoToPage((STATE.currentPage + 1) % PAGE_COUNT, true); }
  else if (k === 'p' || k === 'ArrowLeft') { closeSheetNow(); navGoToPage((STATE.currentPage - 1 + PAGE_COUNT) % PAGE_COUNT, false); }
  else if (k === 'w') { closeSheetNow(); navOpenSheet(0); }
  else if (k === 'd') { closeSheetNow(); navOpenSheet(1); }
  else if (k === 's') { closeSheetNow(); navOpenSheet(2); }
  else if (k === 'x' || k === 'Escape') navCloseSheet();
  else if (k === 'm') motionTimeScale = motionTimeScale > 1 ? 1 : 10;
  else return;
  event.preventDefault();
});

document.getElementById('poll-now').addEventListener('click', fetchUsage);
document.getElementById('poll-interval').addEventListener('change', restartPollTimer);
document.getElementById('sim-offline').addEventListener('change', fetchUsage);
document.getElementById('data-source').addEventListener('change', (e) => {
  lsSet(DATA_SOURCE_KEY, e.target.value);
  fetchUsage();
});
document.getElementById('sim-sd-missing').addEventListener('change', () => { if (!navTransitionActive()) redrawScreen(); });
let hourlyTestStartMs = 0;  // serial 'h' twin
document.getElementById('hourly-signal').addEventListener('click', () => { hourlyTestStartMs = performance.now(); });
