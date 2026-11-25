// sim/net.js -- polling and applying /api/usage (src/net.cpp).

// ── DATA LOADING ──────────────────────────────────────────────
function saveLastUsageCache(doc) { lsSet(LAST_USAGE_KEY, JSON.stringify(doc)); }
function loadCachedUsage() {
  try {
    const raw = lsGet(LAST_USAGE_KEY);
    if (!raw) return false;
    applyUsageDoc(JSON.parse(raw), false);
    return true;
  } catch { return false; }
}

// Mirrors net.cpp's applyUsageJson: keep last-known limits/btc/weather/aqi on
// an absent field; the note always overwrites (it is server-local state).
function applyUsageDoc(doc, fromNetwork = true) {
  if (fromNetwork) STATE.firmwareOffer = doc.firmware?.s3 ?? null;  // ota.cpp: live polls only
  STATE.projects = (doc.projects || []).slice(0, 5);
  STATE.trend = Array.from({ length: 7 }, (_, i) => doc.trend?.[i] ?? 0);
  if (doc.limits) {
    STATE.sessionPercent = doc.limits.session?.percent ?? -1;
    STATE.sessionResets = doc.limits.session?.resets ?? '';
    STATE.sessionResetsInSec = doc.limits.session?.resets_in_sec ?? -1;
    STATE.weekPercent = doc.limits.week?.percent ?? -1;
    STATE.weekResets = doc.limits.week?.resets ?? '';
    STATE.weekResetsInSec = doc.limits.week?.resets_in_sec ?? -1;
    STATE.weekModelPercent = doc.limits.week_model?.percent ?? -1;
    STATE.weekModelName = doc.limits.week_model?.name ?? '';
    STATE.weekModelResets = doc.limits.week_model?.resets ?? '';
    STATE.creditsUsed = doc.limits.credits?.used ?? -1;
    STATE.creditsLimit = doc.limits.credits?.limit ?? -1;
    STATE.creditsPercent = doc.limits.credits?.percent ?? -1;
  }
  STATE.ctxTokens = doc.context?.tokens ?? -1;
  STATE.ctxPercent = doc.context?.percent ?? -1;
  if (doc.btc && doc.btc.price > 0) {
    STATE.btcPrice = doc.btc.price;
    if (typeof doc.btc.changePct === 'number') STATE.btcChangePct = doc.btc.changePct;
  }
  if (doc.weather) applyWeatherDoc(doc.weather);
  if (doc.aqi && doc.aqi.aqi >= 0) STATE.aqi = doc.aqi.aqi;
  if (doc.note) {
    STATE.note = doc.note.text || '';
    STATE.noteSize = Math.min(3, Math.max(1, doc.note.size || 1));
  }
  // Battery Save AUTO follows live polls only, never the cache (net.cpp).
  if (fromNetwork && doc.power && typeof doc.power.battery_save === 'boolean') {
    const want = !!doc.power.battery_save;
    if (want !== STATE.serverBatterySave) {
      STATE.serverBatterySave = want;
      restartPollTimer();
    }
  }
  STATE.lastFetchOkMs = performance.now();
  showRawDoc(doc);
}

function applyWeatherDoc(w) {
  if (!w) return;
  if (w.tempC != null) STATE.weatherTempC = w.tempC;
  if (w.code != null) STATE.weatherCode = w.code;
  if (w.high != null) STATE.weatherHigh = Math.round(w.high);
  if (w.low != null) STATE.weatherLow = Math.round(w.low);
  if (w.condition != null) STATE.weatherCondition = w.condition;
  if (Array.isArray(w.hourly)) {
    STATE.weatherHourly = w.hourly.slice(0, WEATHER_HOURLY_N).map(h => ({
      h: h.h ?? -1, tempC: Math.round(h.tempC ?? 0), code: h.code ?? -1,
    }));
  }
  if (Array.isArray(w.daily)) {
    STATE.weatherDaily = w.daily.slice(0, WEATHER_DAILY_N).map(d => ({
      wd: d.wd ?? -1, high: Math.round(d.high ?? 0), low: Math.round(d.low ?? 0), code: d.code ?? -1,
    }));
  }
  lsSet(LAST_WEATHER_KEY, JSON.stringify(w));
}
function loadWeatherCache() {
  try {
    const raw = lsGet(LAST_WEATHER_KEY);
    if (raw) applyWeatherDoc(JSON.parse(raw));
  } catch { /* ignore corrupt cache */ }
}

function markFetchFailed() {
  const now = performance.now();
  if (now - STATE.lastFetchSuccessMs >= offlineAfterMs()) STATE.haveData = false;
  else if (!STATE.haveData) STATE.haveData = loadCachedUsage();
}

let fetchInFlight = false;
let lastMockReason = '';  // logs the switch to mock data once, not every poll
async function fetchUsage() {
  if (fetchInFlight) return;
  fetchInFlight = true;
  STATE.lastPollMs = performance.now();
  const offline = STATE.simOffline;
  const source = STATE.simDataSource;
  const ok = (detail) => {
    STATE.haveData = true;
    STATE.pollOkSeq++;  // one pace sweep (main.cpp's pollOkSeq)
    STATE.lastFetchSuccessMs = performance.now();
    STATE.wifiOk = true;
    STATE.connected = true;
    setConn(true, detail);
  };
  // Mock data stands in for the server; it is never written to the
  // last-usage cache, which (like the board's SD copy) holds real polls only.
  const useMock = (reason) => {
    applyUsageDoc(mockDoc());
    ok(reason);
    if (reason !== lastMockReason) log('using mock data: ' + reason, 'ok');
    lastMockReason = reason;
  };
  try {
    if (offline) {
      STATE.wifiOk = false;
      STATE.connected = false;
      markFetchFailed();
      setConn(false, 'simulated offline');
      log('poll skipped: simulated offline', 'err');
      lastMockReason = '';
      return;
    }
    if (source === 'mock') { useMock('mock only'); return; }
    const url = STATE.simServerUrl;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), 5000);
    try {
      const res = await fetch(url, { signal: controller.signal });
      if (!res.ok) throw new Error('HTTP ' + res.status);
      const doc = await res.json();
      applyUsageDoc(doc);
      saveLastUsageCache(doc);
      lastMockReason = '';
      ok('ok, ' + new Date().toLocaleTimeString());
      log('fetched ' + fmtTokens(doc.today?.tokens ?? 0) + ' today / ' + fmtTokens(doc.week?.tokens ?? 0) + ' week', 'ok');
    } catch (err) {
      if (source === 'auto') {
        useMock('no server (' + (err.name === 'AbortError' ? 'timeout' : err.message) + ')');
        return;
      }
      STATE.wifiOk = false;
      STATE.connected = false;
      markFetchFailed();
      setConn(false, err.name === 'AbortError' ? 'timeout (5s)' : err.message);
      log('fetch failed: ' + err.message, 'err');
    } finally {
      clearTimeout(timer);
    }
  } finally {
    fetchInFlight = false;
  }
}

function restartPollTimer() {
  clearInterval(STATE.pollTimer);
  STATE.pollTimer = setInterval(fetchUsage, applyEffectivePoll());
}
