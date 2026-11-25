// sim/page_note.js -- Note page and the note tokenizer (four-copy parity surface).

// ── NOTE PAGE (pages.cpp twin) ────────────────────────────────
const NOTE_X = TOK_LAYOUT_COL_RIGHT_X, NOTE_Y = TOK_LAYOUT_CONTENT_Y0;
const NOTE_W = TOK_LAYOUT_COL_RIGHT_W, NOTE_H = TOK_LAYOUT_CONTENT_Y1 - TOK_LAYOUT_CONTENT_Y0;
const NOTE_TX = NOTE_X + TOK_SPACE_CARD_PAD;
const NOTE_TY = NOTE_Y + TOK_SPACE_CARD_PAD + TOK_LINE_CAPTION + TOK_SPACE_SM;
const NOTE_TY_MAX = NOTE_Y + NOTE_H - TOK_SPACE_CARD_PAD;
const NOTE_TEXT_W = NOTE_W - 2 * TOK_SPACE_CARD_PAD;
const NOTE_FONTS = [TOK_TYPE_MONO_S, TOK_TYPE_MONO_M, TOK_TYPE_MONO_L];

// ── Syntax highlighting ───────────────────────────────────────────────────
// Same rule set as src/pages.cpp, note.html and tools/note.py. Precedence, first match wins:
//   1. inside a `backtick span`      -> note.code (delimiters included)
//   2. word is TODO/FIXME/BUG        -> note.keyword.bad
//      word is DONE/OK               -> note.keyword.good
//      word is numeric               -> note.number
//   3. line begins '#'               -> note.heading (whole line)
//      line begins '>'               -> note.quote   (whole line)
//   4. leading "- ", "* ", "+ "      -> note.marker (the marker char only)
//   5. otherwise                     -> note.text
// Keywords are matched case-SENSITIVE uppercase-only.
function notePunctOpen(c) { return c === '(' || c === '[' || c === '{' || c === '"' || c === "'"; }
function notePunctClose(c) {
  return c === ')' || c === ']' || c === '}' || c === ':' || c === ';' ||
         c === ',' || c === '.' || c === '!' || c === '?' || c === '"' || c === "'";
}
function noteIsDigit(c) { return c >= '0' && c <= '9'; }
function noteIsNumChar(c) {
  return noteIsDigit(c) || c === '.' || c === ',' || c === ':' || c === '/' ||
         c === '%' || c === '$' || c === '+' || c === '-';
}
function noteWordColor(s, a, b) {
  while (a < b && notePunctOpen(s[a])) a++;
  while (b > a && notePunctClose(s[b - 1])) b--;
  if (b === a) return null;
  const w = s.slice(a, b);
  if (w === 'TODO' || w === 'FIXME' || w === 'BUG') return TOK_NOTE_KEYWORD_BAD;
  if (w === 'DONE' || w === 'OK') return TOK_NOTE_KEYWORD_GOOD;
  let hasDigit = false;
  for (let i = a; i < b; i++) {
    if (!noteIsNumChar(s[i])) return null;
    if (noteIsDigit(s[i])) hasDigit = true;
  }
  return hasDigit ? TOK_NOTE_NUMBER : null;
}

// Word-wrapped, syntax-coloured STATE.note: glyph columns from tx, rows from
// ty, widthPx wide, clipped at tyMax (exclusive). pages.cpp's drawNoteText.
function drawNoteText(tx, ty, widthPx, tyMax, font) {
  const s = STATE.note || '';
  const glyphW = glyphOf(font, '0').adv;
  const glyphH = fontLineH(font);
  const lineH = glyphH;
  const cols = Math.floor(widthPx / glyphW);
  let y = ty, col = 0, i = 0;
  const n = s.length;
  while (i < n) {
    let lineEnd = i;
    while (lineEnd < n && s[lineEnd] !== '\n') lineEnd++;
    let p = i;
    while (p < lineEnd && s[p] === ' ') p++;
    let lineColor = null, markerIdx = -1;
    if (p < lineEnd) {
      if (s[p] === '#') lineColor = TOK_NOTE_HEADING;
      else if (s[p] === '>') lineColor = TOK_COLOR_TEXT_SECONDARY;  // note.quote
      else if ((s[p] === '-' || s[p] === '*' || s[p] === '+') && (p + 1 === lineEnd || s[p + 1] === ' ')) markerIdx = p;
    }
    let inCode = false;
    col = 0;
    while (i < lineEnd) {
      if (s[i] === ' ') {
        if (col > 0) col++;
        i++;
        if (col >= cols) { y += lineH; col = 0; if (y + glyphH > tyMax) return; }
        continue;
      }
      let wEnd = i;
      while (wEnd < lineEnd && s[wEnd] !== ' ') wEnd++;
      const wLen = wEnd - i;
      if (col > 0 && col + wLen > cols && wLen <= cols) {
        y += lineH; col = 0;
        if (y + glyphH > tyMax) return;
        continue;
      }
      const wordColor = noteWordColor(s, i, wEnd);
      while (i < wEnd) {
        if (col >= cols) { y += lineH; col = 0; if (y + glyphH > tyMax) return; }
        let c;
        if (s[i] === '`') { inCode = !inCode; c = TOK_NOTE_CODE; }
        else if (inCode) c = TOK_NOTE_CODE;
        else if (wordColor) c = wordColor;
        else if (lineColor) c = lineColor;
        else if (i === markerIdx) c = TOK_NOTE_MARKER;
        else c = TOK_NOTE_TEXT;
        drawChar(font, tx + col * glyphW, y, s[i], c);
        col++; i++;
      }
    }
    i = lineEnd + 1;
    y += lineH; col = 0;
    if (y + glyphH > tyMax) return;
  }
}
// The right column as one card: 'NOTE', then the text at the editor's size.
function drawNotePane() {
  drawCard(NOTE_X, NOTE_Y, NOTE_W, NOTE_H);
  drawSectionLabel(NOTE_TX, NOTE_Y + TOK_SPACE_CARD_PAD, 'NOTE');
  if (!STATE.note) {
    drawEmptyState(NOTE_X + NOTE_W / 2, NOTE_Y, NOTE_H, 'No note yet', 'Edit at :8787/note', false);
    return;
  }
  drawNoteText(NOTE_TX, NOTE_TY, NOTE_TEXT_W, NOTE_TY_MAX, NOTE_FONTS[Math.min(3, Math.max(1, STATE.noteSize || 1)) - 1]);
}
function drawNotePage() { drawLimitsColumn(); drawBtcCard(); drawNotePane(); }
