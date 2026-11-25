// Note page (page 5): tokenizer, wrap, draw.
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"

// ── NOTE PAGE (NOTE_PAGE) ──────────────────────────────────
// The right column as one card: "NOTE" label, then the text in type.mono.*
// from the first row below the corner slots.
static const int NOTE_X = TOK_LAYOUT_COL_RIGHT_X;
static const int NOTE_Y = TOK_LAYOUT_CONTENT_Y0;
static const int NOTE_W = TOK_LAYOUT_COL_RIGHT_W;
static const int NOTE_H = TOK_LAYOUT_CONTENT_Y1 - TOK_LAYOUT_CONTENT_Y0;
static const int NOTE_TX = NOTE_X + TOK_SPACE_CARD_PAD;            // 200, first glyph column
static const int NOTE_TY = NOTE_Y + TOK_SPACE_CARD_PAD + 17 + TOK_SPACE_SM;  // 45 (>= 40, clear of the corner)
// Exclusive bottom limit: a row is drawn only while y + lineH <= this.
static const int NOTE_TY_MAX = NOTE_Y + NOTE_H - TOK_SPACE_CARD_PAD;  // 268
// Usable width 304. Monospace advance per size (make_vlw.py):
//   size | font  | adv | step | cols | rows
//     1  | MONO1 |  7  |  15  |  37  |  14
//     2  | MONO2 |  10 |  21  |  26  |  10
//     3  | MONO3 |  13 |  29  |  20  |   7
// Every size holds at least the CYD pane's columns (24/12/8), so a note
// written against the CYD's note.html fit check wraps no worse here.
static const int NOTE_TEXT_W = NOTE_W - 2 * TOK_SPACE_CARD_PAD;
static const FontId NOTE_FONTS[3] = {TOK_TYPE_MONO_S, TOK_TYPE_MONO_M, TOK_TYPE_MONO_L};

// ── Syntax highlighting ───────────────────────────────────────────────────
// This rule set is duplicated in simulator-s3.html, note.html and
// tools/note.py; all must agree or the board and the
// editor disagree about what the text looks like. Tokenizing happens at two
// levels -- source line, then word -- plus a one-character backtick toggle.
// There is deliberately no per-character classification. The colours are
// design.md's note.* tokens (section 11.19).
//
// Precedence, first match wins:
//   1. inside a `backtick span`      -> note.code (delimiters included)
//   2. word is TODO/FIXME/BUG        -> note.keyword.bad
//      word is DONE/OK               -> note.keyword.good
//      word is numeric               -> note.number
//   3. line begins '#'               -> note.heading (whole line)
//      line begins '>'               -> note.quote   (whole line)
//   4. leading "- ", "* ", "+ "      -> note.marker (the marker char only)
//   5. otherwise                     -> note.text
//
// Keywords are matched case-SENSITIVE uppercase-only.
static bool notePunctOpen(char c) {
  return c == '(' || c == '[' || c == '{' || c == '"' || c == '\'';
}

static bool notePunctClose(char c) {
  return c == ')' || c == ']' || c == '}' || c == ':' || c == ';' ||
         c == ',' || c == '.' || c == '!' || c == '?' || c == '"' || c == '\'';
}

static bool noteIsDigit(char c) { return c >= '0' && c <= '9'; }

// Characters allowed inside a numeric token alongside the digits, so
// "12:30", "$1,200", "2026-08-26" and "50%" each colour as one unit.
static bool noteIsNumChar(char c) {
  return noteIsDigit(c) || c == '.' || c == ',' || c == ':' || c == '/' ||
         c == '%' || c == '$' || c == '+' || c == '-';
}

static bool noteRangeEquals(const char* s, int a, int b, const char* w) {
  int n = strlen(w);
  return (b - a) == n && strncmp(s + a, w, n) == 0;
}

// Word-level colour for s[a..b), or 0 for "no override". Wrapping punctuation
// is trimmed for the comparison only.
static uint16_t noteWordColor(const char* s, int a, int b) {
  while (a < b && notePunctOpen(s[a])) a++;
  while (b > a && notePunctClose(s[b - 1])) b--;
  if (b == a) return 0;
  if (noteRangeEquals(s, a, b, "TODO") || noteRangeEquals(s, a, b, "FIXME") ||
      noteRangeEquals(s, a, b, "BUG")) return TOK_NOTE_KEYWORD_BAD;
  if (noteRangeEquals(s, a, b, "DONE") || noteRangeEquals(s, a, b, "OK")) return TOK_NOTE_KEYWORD_GOOD;
  bool hasDigit = false;
  for (int i = a; i < b; i++) {
    if (!noteIsNumChar(s[i])) return 0;
    if (noteIsDigit(s[i])) hasDigit = true;
  }
  return hasDigit ? TOK_NOTE_NUMBER : 0;
}

// Word-wrapped, syntax-coloured render of STATE.note: glyph columns from tx,
// rows from ty, widthPx wide. Overflow past tyMax (exclusive) is simply not
// drawn ("wrap + clip"). Caller holds stateMutex; this must not re-lock.
void drawNoteText(int tx, int ty, int widthPx, int tyMax, FontId font) {
  const char* s = STATE.note;
  const int glyphW = FONT_ADV[font]['0' - 0x20];  // monospace: every advance is equal
  const int glyphH = fontLineH(font);
  const int lineH = glyphH;
  const int cols = widthPx / glyphW;

  int y = ty;
  int col = 0;
  const int n = strlen(s);
  int i = 0;

  while (i < n) {
    // ── extent of this source line ──
    int lineEnd = i;
    while (lineEnd < n && s[lineEnd] != '\n') lineEnd++;

    // ── line-level context, decided once ──
    int p = i;
    while (p < lineEnd && s[p] == ' ') p++;  // allow indented markers
    uint16_t lineColor = 0;
    int markerIdx = -1;
    if (p < lineEnd) {
      if (s[p] == '#') lineColor = TOK_NOTE_HEADING;
      else if (s[p] == '>') lineColor = TOK_COLOR_TEXT_SECONDARY;  // note.quote
      else if ((s[p] == '-' || s[p] == '*' || s[p] == '+') &&
               (p + 1 == lineEnd || s[p + 1] == ' ')) markerIdx = p;
    }
    bool inCode = false;  // an unmatched backtick stains only its own line
    col = 0;

    // ── emit the source line as one or more visual rows ──
    while (i < lineEnd) {
      if (s[i] == ' ') {
        // Leading spaces on a wrapped row collapse; a source line's own
        // indentation collapses too.
        if (col > 0) col++;
        i++;
        if (col >= cols) {
          y += lineH; col = 0;
          if (y + glyphH > tyMax) return;
        }
        continue;
      }

      // Word lookahead: [i, wEnd) is the next run of non-space characters.
      int wEnd = i;
      while (wEnd < lineEnd && s[wEnd] != ' ') wEnd++;
      const int wLen = wEnd - i;

      // Wrap before a word that doesn't fit here but would fit on a fresh
      // row. A word longer than a whole row is hard-broken below instead.
      if (col > 0 && col + wLen > cols && wLen <= cols) {
        y += lineH; col = 0;
        if (y + glyphH > tyMax) return;
        continue;  // re-test on the new row
      }

      const uint16_t wordColor = noteWordColor(s, i, wEnd);

      while (i < wEnd) {
        if (col >= cols) {  // hard break inside an over-long word
          y += lineH; col = 0;
          if (y + glyphH > tyMax) return;
        }
        uint16_t c;
        if (s[i] == '`') {
          inCode = !inCode;
          c = TOK_NOTE_CODE;  // colour the tick itself, so a stray one is visible
        } else if (inCode) {
          c = TOK_NOTE_CODE;
        } else if (wordColor) {
          c = wordColor;
        } else if (lineColor) {
          c = lineColor;
        } else if (i == markerIdx) {
          c = TOK_NOTE_MARKER;
        } else {
          c = TOK_NOTE_TEXT;
        }
        drawChar(font, tx + col * glyphW, y, s[i], c);
        col++; i++;
      }
    }

    // ── end of source line (an empty line consumes a row, as an editor implies) ──
    i = lineEnd + 1;  // step over the '\n'
    y += lineH; col = 0;
    if (y + glyphH > tyMax) return;
  }
}

// The right column as one card: "NOTE", then the text at the editor's size.
static void drawNotePane() {
  drawCard(NOTE_X, NOTE_Y, NOTE_W, NOTE_H);
  drawSectionLabel(NOTE_TX, NOTE_Y + TOK_SPACE_CARD_PAD, "NOTE");
  if (STATE.note[0] == '\0') {
    drawEmptyState(NOTE_X + NOTE_W / 2, NOTE_Y, NOTE_H, "No note yet", "Edit at :8787/note", false);
    return;
  }
  drawNoteText(NOTE_TX, NOTE_TY, NOTE_TEXT_W, NOTE_TY_MAX, NOTE_FONTS[constrain(STATE.noteSize, 1, 3) - 1]);
}

// Page 6: the shared left column, with the note pane where the cats go.
void drawNotePage() {
  drawLimitsColumn();
  drawBtcCard();
  drawNotePane();
}
