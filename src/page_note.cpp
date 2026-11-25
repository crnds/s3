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
static const int NOTE_TY = NOTE_Y + TOK_SPACE_CARD_PAD + TOK_LINE_CAPTION + TOK_SPACE_SM;  // 45 (>= 40, clear of the corner)
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

namespace {
struct NoteDraw {
  const char* s;
  int tx, ty, glyphW, lineH;
  FontId font;
};

// The draw half: a placed glyph -> its colour (code, then role, then marker).
void drawNoteCell(void* ctx, const NoteCell& cell) {
  const NoteDraw& d = *static_cast<const NoteDraw*>(ctx);
  uint16_t c;
  if (cell.code) c = TOK_NOTE_CODE;
  else if (cell.role != NOTE_ROLE_NONE) c = noteRoleColor(cell.role);
  else if (cell.marker) c = TOK_NOTE_MARKER;
  else c = TOK_NOTE_TEXT;
  drawChar(d.font, d.tx + cell.col * d.glyphW, d.ty + cell.row * d.lineH, d.s[cell.idx], c);
}
}  // namespace

// Word-wrapped, syntax-coloured render of STATE.note: glyph columns from tx,
// rows from ty, widthPx wide. Overflow past tyMax (exclusive) is simply not
// drawn ("wrap + clip"). Caller holds stateMutex; this must not re-lock.
void drawNoteText(int tx, int ty, int widthPx, int tyMax, FontId font) {
  NoteDraw d = {STATE.note, tx, ty, FONT_ADV[font]['0' - 0x20] /* monospace: all equal */,
                fontLineH(font), font};
  const int glyphH = d.lineH;
  // A row r is drawn while ty + r * lineH + glyphH <= tyMax; the first row always is.
  const int maxRows = tyMax - ty >= glyphH ? (tyMax - ty - glyphH) / d.lineH + 1 : 1;
  noteWrap(d.s, strlen(d.s), widthPx / d.glyphW, maxRows, drawNoteCell, &d);
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
