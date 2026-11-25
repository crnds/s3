// sim/format.js -- number and time text (src/format.cpp).

// ── FORMATTING (mirroring format.cpp) ─────────────────────────
function fmtTokens(t) {
  if (t >= 1e9) return (t / 1e9).toFixed(2) + 'B';
  if (t >= 1e6) return (t / 1e6).toFixed(1) + 'M';
  if (t >= 1e3) return (t / 1e3).toFixed(1) + 'K';
  return String(t);
}
function fmtBtc(p) { return p < 0 ? '--' : Math.round(p).toLocaleString('en-US'); }
function fmtChangePct(pct) { return (pct < 0 ? '-' : '+') + Math.trunc(Math.abs(pct) + 0.5) + '%'; }
function fmtCountdown(sec) {
  if (sec < 0) return '';
  let m = Math.floor((sec + 30) / 60);
  const h = Math.floor(m / 60);
  m %= 60;
  return h > 0 ? h + 'h ' + String(m).padStart(2, '0') + 'm' : m + 'm';
}
function fmtCountdownDHM(sec) {
  if (sec < 0) return '';
  const totalMin = Math.floor((sec + 30) / 60);
  const days = Math.floor(totalMin / 1440);
  if (days > 0) return days + 'd ' + Math.floor((totalMin % 1440) / 60) + 'h';
  return Math.floor(totalMin / 60) + 'h ' + String(totalMin % 60).padStart(2, '0') + 'm';
}
function fmtKB(bytes) { return (bytes / 1024).toFixed(1) + 'KB'; }
function fmtGB(bytes) { return (bytes / (1024 ** 3)).toFixed(1) + 'GB'; }
const WDAY_ABBR = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
const WDAY_FULL = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];
