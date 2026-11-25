// sim/mock.js -- sim-only: the built-in MOCK usage document.

const MOCK = {
  today: { tokens: 2573012, cost: 2.31 },
  week: { tokens: 351668302, cost: 228.27 },
  active_now: true,
  projects: [
    { name: 'ampypay-eunite', tokens: 178178755 },
    { name: 'org-chart', tokens: 145575128 },
    { name: 'autoclaude', tokens: 15567872 },
    { name: 'iief-thai', tokens: 5286431 },
    { name: 'fsfi-thai', tokens: 3970315 },
  ],
  trend: [118891173, 158279627, 0, 0, 55363947, 17326402, 2573012],
  limits: {
    session: { percent: 57, resets: '03:19', resets_in_sec: 5400 },
    week: { percent: 86, resets: 'Jul 9, 04:59', resets_in_sec: 302400 },
    week_model: { name: 'Fable', percent: 58, resets: 'Jul 16, 04:59' },
    credits: { used: 0.41, limit: 10.0, percent: 4 },
  },
  context: { tokens: 80000, percent: 40 },
  btc: { price: 109432, changePct: 2.31 },
  aqi: { aqi: 81 },
  // Exercises every tokenizer rule at once (same sample the CYD simulator used).
  note: {
    text: '# SPRINT 6\n> shipped 2026-08-26\n\n- TODO wire the note pane\n' +
          '* DONE server /api/note\n+ OK boot page picker\n\n' +
          'budget $1,200 / 50% used\nrun `arduino-cli compile --fqbn esp32`\n' +
          'supercalifragilisticexpialidocious',
    size: 1,
  },
  weather: {
    tempC: 34, code: 3, high: 35, low: 26, condition: 'Overcast',
    hourly: [
      { h: 17, tempC: 34, code: 3 }, { h: 18, tempC: 33, code: 2 }, { h: 19, tempC: 32, code: 0 },
      { h: 20, tempC: 31, code: 0 }, { h: 21, tempC: 30, code: 0 }, { h: 22, tempC: 29, code: 2 },
      { h: 23, tempC: 29, code: 3 }, { h: 0, tempC: 28, code: 61 }, { h: 1, tempC: 28, code: 61 },
      { h: 2, tempC: 27, code: 3 }, { h: 3, tempC: 27, code: 2 },
    ],
    daily: [
      { wd: 1, high: 35, low: 26, code: 51 }, { wd: 2, high: 35, low: 27, code: 95 },
      { wd: 3, high: 35, low: 26, code: 95 }, { wd: 4, high: 34, low: 26, code: 95 },
      { wd: 5, high: 32, low: 25, code: 51 },
    ],
  },
  power: { on_ac: true, percent: 82, paused: false, battery_save: false, battery_save_manual: null, source: 'auto' },
};

// MOCK shifted to now (Bangkok time) so it reads as live data: each reset
// lands where its countdown says, the hourly forecast starts at this hour and
// the daily one tomorrow.
function mockDoc() {
  const doc = structuredClone(MOCK);
  const bkkNow = simNowMs() + 7 * 3600 * 1000;  // a Date's UTC fields = Bangkok wall time
  const at = (sec) => new Date(bkkNow + sec * 1000);
  const hm = (d) => String(d.getUTCHours()).padStart(2, '0') + ':' + String(d.getUTCMinutes()).padStart(2, '0');
  const fmtReset = (d) => `${MON_ABBR_LIST[d.getUTCMonth()]} ${d.getUTCDate()}, ${hm(d)}`;
  const lim = doc.limits;
  lim.session.resets = hm(at(lim.session.resets_in_sec));
  lim.week.resets = fmtReset(at(lim.week.resets_in_sec));
  lim.week_model.resets = fmtReset(at(lim.week.resets_in_sec + 7 * 86400));
  const now = at(0);
  doc.weather.hourly.forEach((h, i) => { h.h = (now.getUTCHours() + i) % 24; });
  doc.weather.daily.forEach((d, i) => { d.wd = (now.getUTCDay() + 1 + i) % 7; });
  return doc;
}
