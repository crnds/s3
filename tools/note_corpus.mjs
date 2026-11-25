// Note tokenizer corpus check for the browser copies (roadmap O5; CI's
// "simulator" job). Runs tests/note_tokens.json against noteWordColor in
// simulator-s3.html and note.html, and noteLineContext in note.html (the
// simulator draws lines inline, so it only gets the word cases). The Python copy
// (tools/note.py) runs the same corpus in tests/server/test_note_tokens.py.
//
//   node tools/note_corpus.mjs
//
// Needs Playwright, same as tools/sim_check.mjs.
import { createRequire } from 'node:module';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const corpus = JSON.parse(fs.readFileSync(path.join(ROOT, 'tests', 'note_tokens.json'), 'utf8'));

const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.png': 'image/png' };
const server = http.createServer((req, res) => {
  const file = path.join(ROOT, decodeURIComponent(new URL(req.url, 'http://x').pathname));
  if (!file.startsWith(ROOT) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) {
    res.writeHead(404).end();
    return;
  }
  res.writeHead(200, { 'Content-Type': TYPES[path.extname(file)] || 'application/octet-stream' });
  fs.createReadStream(file).pipe(res);
});
await new Promise(r => server.listen(0, '127.0.0.1', r));
const base = `http://127.0.0.1:${server.address().port}`;

// Runs in the page. `names` maps each copy's colour constants to corpus roles;
// the simulator's colours are theme tokens (dark theme at load), note.html's
// are COL_* constants. Anything that is not a known role is reported as "?name".
function run({ words, lines, copy }) {
  const roles = copy === 'sim'
    ? { bad: TOK_NOTE_KEYWORD_BAD, good: TOK_COLOR_STATUS_SUCCESS, number: TOK_COLOR_CONTENT_SUN }
    : { bad: COL_WARN, good: COL_GOOD, number: COL_YELLOW };
  const lineRoles = copy === 'sim' ? null : { heading: COL_ACCENT, quote: COL_TEXT2 };
  const roleOf = (table, c) => {
    if (c === null || c === undefined) return null;
    for (const [name, v] of Object.entries(table)) if (v === c) return name;
    return '?' + c;
  };
  const out = [];
  for (const c of words) {
    const got = roleOf(roles, noteWordColor(c.w, 0, c.w.length));
    if (got !== c.role) out.push(`word ${JSON.stringify(c.w)}: expected ${c.role}, got ${got}`);
  }
  if (lineRoles) {
    for (const c of lines) {
      const r = noteLineContext(c.s, c.start, c.s.length);
      const got = { end: r.lineEnd, line: roleOf(lineRoles, r.lineColor), marker: r.markerIdx };
      const want = { end: c.end, line: c.line, marker: c.marker };
      if (JSON.stringify(got) !== JSON.stringify(want)) {
        out.push(`line ${JSON.stringify(c.s)}: expected ${JSON.stringify(want)}, got ${JSON.stringify(got)}`);
      }
    }
  }
  return out;
}

const browser = await chromium.launch({ args: ['--disable-gpu'] });
let failures = 0;
try {
  for (const [copy, url] of [['sim', '/simulator-s3.html?fixture=default'], ['note.html', '/note.html']]) {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    await page.goto(base + url);
    if (copy === 'sim') await page.waitForFunction(() => window.simFixtureReady || window.simFixtureError, null, { timeout: 15000 });
    const bad = await page.evaluate(run, { words: corpus.words, lines: corpus.lines, copy });
    const n = corpus.words.length + (copy === 'sim' ? 0 : corpus.lines.length);
    if (bad.length || errors.length) {
      failures += bad.length + errors.length;
      for (const b of bad) console.log(`FAIL ${copy}: ${b}`);
      for (const e of errors) console.log(`JS ERROR ${copy}: ${e}`);
    } else {
      console.log(`ok   ${copy}: ${n} corpus cases`);
    }
    await page.close();
  }
} finally {
  await browser.close();
  server.close();
}
if (failures) {
  console.log(`${failures} failure(s)`);
  process.exit(1);
}
