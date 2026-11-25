// Note page (page 5): tokenizer, wrap, draw.
// Split from the old pages.cpp (roadmap O10); twin: the simulator file of the same name.
#include "state.h"
#include "pages_common.h"
#include "note_pure.h"

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
// The rules (note_pure.cpp) are duplicated in sim/page_note.js, note.html and
// tools/note.py; all four run tests/note_tokens.json. Tokenizing happens at two
// levels -- source line, then word -- plus a one-character backtick toggle.
// There is deliberately no per-character classification. The colours are
// design.md's note.* tokens (section 11.19), chosen here from the roles.
static uint16_t noteRoleColor(NoteRole r) {
  switch (r) {
    case NOTE_ROLE_BAD:     return TOK_NOTE_KEYWORD_BAD;
    case NOTE_ROLE_GOOD:    return TOK_NOTE_KEYWORD_GOOD;
    case NOTE_ROLE_NUMBER:  return TOK_NOTE_NUMBER;
    case NOTE_ROLE_HEADING: return TOK_NOTE_HEADING;
    case NOTE_ROLE_QUOTE:   return TOK_COLOR_TEXT_SECONDARY;  // note.quote
    default:                return TOK_NOTE_TEXT;  // NOTE_ROLE_NONE: callers test the role, never a colour
  }
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
    // ── line-level context, decided once ──
    const NoteLine ln = noteLineContext(s, i, n);
    const int lineEnd = ln.end;
    const int markerIdx = ln.marker;
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

      const NoteRole wordRole = noteWordRole(s, i, wEnd);

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
        } else if (wordRole != NOTE_ROLE_NONE) {
          c = noteRoleColor(wordRole);
        } else if (ln.role != NOTE_ROLE_NONE) {
          c = noteRoleColor(ln.role);
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
