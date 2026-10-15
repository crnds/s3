// Headless simulator check (roadmap O8; CI's "simulator" job).
//
//   node tools/sim_check.mjs            compare with tests/golden/
//   node tools/sim_check.mjs --update   rewrite the goldens (commit them)
//
// Serves the repo, opens simulator-s3.html?fixture=<name> in headless Chromium
// and draws every page and sheet (window.simFixture) in both themes. Each one
// is compared pixel for pixel with tests/golden/<fixture>/<screen>-<theme>.png;
// any JS error fails the run too. A mismatch writes the actual image and a
// diff (mismatched pixels red over a dimmed copy) to tests/out/.
//
// Needs Playwright (CI: `npm install --no-save playwright@1.63.0`; locally the
// same, or NODE_PATH pointing at an install). Text is the firmware's glyph
// atlas and shapes are LovyanGFX's integer algorithm, so frames are
// deterministic; TOLERANCE only absorbs 8-bit blending differences between
// machines, never a moved pixel.
import { createRequire } from 'node:module';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const { chromium } = require('playwright');

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const FIXTURES = ['default'];
const THEMES = ['dark', 'light'];
const TOLERANCE = 2;  // per 8-bit channel
const UPDATE = process.argv.includes('--update');
const OUT = path.join(ROOT, 'tests', 'out');

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

// Runs in the page: draw one screen, then compare the canvas with the golden.
function capture({ screen, theme, golden, tolerance }) {
  window.simFixture.show(screen, theme);
  const cv = document.getElementById('screen');
  const actual = cv.getContext('2d').getImageData(0, 0, cv.width, cv.height);
  const png = cv.toDataURL('image/png');
  if (!golden) return Promise.resolve({ png, missing: true });
  return new Promise((resolve, reject) => {
    const img = new Image();
    img.onerror = () => reject(new Error('bad golden for ' + screen));
    img.onload = () => {
      if (img.width !== cv.width || img.height !== cv.height) {
        resolve({ png, mismatched: cv.width * cv.height, maxDelta: 255, sizeChanged: true });
        return;
      }
      const g = document.createElement('canvas');
      g.width = img.width; g.height = img.height;
      const gx = g.getContext('2d');
      gx.drawImage(img, 0, 0);
      const want = gx.getImageData(0, 0, g.width, g.height);
      const diff = gx.createImageData(g.width, g.height);
      let mismatched = 0, maxDelta = 0;
      for (let i = 0; i < want.data.length; i += 4) {
        const d = Math.max(Math.abs(want.data[i] - actual.data[i]), Math.abs(want.data[i + 1] - actual.data[i + 1]),
                           Math.abs(want.data[i + 2] - actual.data[i + 2]));
        maxDelta = Math.max(maxDelta, d);
        const bad = d > tolerance;
        if (bad) mismatched++;
        diff.data[i] = bad ? 255 : actual.data[i] >> 2;
        diff.data[i + 1] = bad ? 0 : actual.data[i + 1] >> 2;
        diff.data[i + 2] = bad ? 0 : actual.data[i + 2] >> 2;
        diff.data[i + 3] = 255;
      }
      gx.putImageData(diff, 0, 0);
      resolve({ png, mismatched, maxDelta, diff: mismatched ? g.toDataURL('image/png') : null });
    };
    img.src = golden;
  });
}

const writePng = (file, dataUrl) => {
  fs.mkdirSync(path.dirname(file), { recursive: true });
  fs.writeFileSync(file, Buffer.from(dataUrl.split(',')[1], 'base64'));
};

const browser = await chromium.launch({ args: ['--disable-gpu', '--force-color-profile=srgb'] });
let failures = 0;
try {
  for (const fixture of FIXTURES) {
    const page = await browser.newPage();
    const errors = [];
    page.on('pageerror', e => errors.push(e.message));
    await page.goto(`${base}/simulator-s3.html?fixture=${fixture}`);
    await page.waitForFunction(() => window.simFixtureReady || window.simFixtureError, null, { timeout: 15000 });
    const fxError = await page.evaluate(() => window.simFixtureError);
    if (fxError) throw new Error(fxError);
    const screens = await page.evaluate(() => window.simFixture.screens);
    for (const theme of THEMES) {
      for (const screen of screens) {
        const name = `${screen}-${theme}`;
        const goldenFile = path.join(ROOT, 'tests', 'golden', fixture, name + '.png');
        const golden = !UPDATE && fs.existsSync(goldenFile)
          ? 'data:image/png;base64,' + fs.readFileSync(goldenFile).toString('base64') : null;
        const r = await page.evaluate(capture, { screen, theme, golden, tolerance: TOLERANCE });
        if (UPDATE) {
          writePng(goldenFile, r.png);
          console.log(`updated  ${fixture}/${name}`);
        } else if (r.missing) {
          failures++;
          writePng(path.join(OUT, fixture, name + '.png'), r.png);
          console.log(`MISSING  ${fixture}/${name} (no golden; run with --update)`);
        } else if (r.mismatched) {
          failures++;
          writePng(path.join(OUT, fixture, name + '.png'), r.png);
          if (r.diff) writePng(path.join(OUT, fixture, name + '-diff.png'), r.diff);
          console.log(`FAIL     ${fixture}/${name}: ${r.mismatched} px differ (max delta ${r.maxDelta})`);
        } else {
          console.log(`ok       ${fixture}/${name}` + (r.maxDelta ? ` (max delta ${r.maxDelta})` : ''));
        }
      }
    }
    if (errors.length) {
      failures += errors.length;
      for (const e of errors) console.log(`JS ERROR ${fixture}: ${e}`);
    }
    await page.close();
  }
} finally {
  await browser.close();
  server.close();
}
if (failures) {
  console.log(`\n${failures} failure(s); actual images and diffs are in tests/out/`);
  process.exit(1);
}
console.log(UPDATE ? '\ngoldens updated' : '\nall screens match');
