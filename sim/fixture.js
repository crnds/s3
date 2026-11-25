// sim/fixture.js -- sim-only: ?fixture=<name> mode for tools/sim_check.mjs.

// ── FIXTURE MODE (?fixture=<name>, roadmap O7/O8) ─────────────
// tests/fixtures/<name>.json = {clock, settings, usage}: the wall clock and
// performance.now() freeze, the usage doc is applied once, nothing polls or
// ticks, and window.simFixture.show(screen, theme) draws any page or sheet
// straight to the canvas -- so tools/sim_check.mjs gets the same pixels on
// every run and compares them with tests/golden/. Needs http(s), not file://.
const FIXTURE_NAME = new URLSearchParams(location.search).get('fixture');
let simClockMs = null;  // a fixture's frozen wall clock (epoch ms), else live
const FIXTURE_PERF_MS = 600000;  // a fixture's frozen performance.now()
function simNowMs() { return simClockMs ?? Date.now(); }
const FIXTURE_SCREENS = ['page0', 'page1', 'page2', 'page3', 'page4', 'page5', 'page6', 'weather', 'device', 'settings'];
async function startFixture(name) {
  const res = await fetch('tests/fixtures/' + encodeURIComponent(name) + '.json');
  if (!res.ok) throw new Error('fixture ' + name + ': HTTP ' + res.status);
  const fx = await res.json();
  simClockMs = Date.parse(fx.clock);
  // A fixed instant, not the load time: the status dot's pulse is a function
  // of performance.now(), so a load-time value made its alpha depend on how
  // fast the page loaded. 10 min is a whole number of pulse periods (full
  // opacity) and safely past every boot-time guard.
  const frozen = FIXTURE_PERF_MS;
  Object.defineProperty(performance, 'now', { value: () => frozen });
  Object.assign(STATE, fx.settings || {});
  applyUsageDoc(fx.usage);
  Object.assign(STATE, { haveData: true, wifiOk: true, connected: true, lastFetchSuccessMs: frozen, lastPollMs: frozen });
  setConn(true, 'fixture ' + name);
  window.simFixture = {
    screens: FIXTURE_SCREENS,
    show(screen, theme) {
      STATE.cfgLightMode = theme === 'light';
      applyThemeTokens();
      STATE.weatherPageOpen = screen === 'weather';
      STATE.devicePageOpen = screen === 'device';
      STATE.settingsScreen = screen === 'settings' ? 'list' : 'off';
      if (screen.startsWith('page')) STATE.currentPage = parseInt(screen.slice(4), 10);
      if (screen === 'settings') renderSettings();
      else render();
    },
  };
  window.simFixtureReady = true;
}
