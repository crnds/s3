// Note tokenizer rules (note_pure.h). Precedence, first match wins:
//   1. inside a `backtick span`      -> note.code (handled by the drawing walk)
//   2. word is TODO/FIXME/BUG        -> bad;  DONE/OK -> good;  numeric -> number
//   3. line begins '#'               -> heading (whole line); '>' -> quote
//   4. leading "- ", "* ", "+ "      -> marker (the marker char only)
//   5. otherwise                     -> note.text
// Keywords are matched case-SENSITIVE uppercase-only.
#include "note_pure.h"

#include <string.h>

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

NoteRole noteWordRole(const char* s, int a, int b) {
  while (a < b && notePunctOpen(s[a])) a++;
  while (b > a && notePunctClose(s[b - 1])) b--;
  if (b == a) return NOTE_ROLE_NONE;
  if (noteRangeEquals(s, a, b, "TODO") || noteRangeEquals(s, a, b, "FIXME") ||
      noteRangeEquals(s, a, b, "BUG")) return NOTE_ROLE_BAD;
  if (noteRangeEquals(s, a, b, "DONE") || noteRangeEquals(s, a, b, "OK")) return NOTE_ROLE_GOOD;
  bool hasDigit = false;
  for (int i = a; i < b; i++) {
    if (!noteIsNumChar(s[i])) return NOTE_ROLE_NONE;
    if (noteIsDigit(s[i])) hasDigit = true;
  }
  return hasDigit ? NOTE_ROLE_NUMBER : NOTE_ROLE_NONE;
}

NoteLine noteLineContext(const char* s, int i, int n) {
  NoteLine L = {i, NOTE_ROLE_NONE, -1};
  while (L.end < n && s[L.end] != '\n') L.end++;
  int p = i;
  while (p < L.end && s[p] == ' ') p++;  // allow indented markers
  if (p < L.end) {
    if (s[p] == '#') L.role = NOTE_ROLE_HEADING;
    else if (s[p] == '>') L.role = NOTE_ROLE_QUOTE;
    else if ((s[p] == '-' || s[p] == '*' || s[p] == '+') &&
             (p + 1 == L.end || s[p + 1] == ' ')) L.marker = p;
  }
  return L;
}

int noteWrap(const char* s, int n, int cols, int maxRows, NoteCellFn emit, void* ctx) {
  if (maxRows < 1) maxRows = 1;
  int row = 0, col = 0, i = 0;
  // Start a new visual row; false once the grid is full.
  auto nextRow = [&]() { row++; col = 0; return row < maxRows; };

  while (i < n) {
    const NoteLine ln = noteLineContext(s, i, n);
    const int lineEnd = ln.end;
    bool inCode = false;  // an unmatched backtick stains only its own line
    col = 0;

    while (i < lineEnd) {
      if (s[i] == ' ') {
        // Leading spaces on a wrapped row collapse; a source line's own
        // indentation collapses too.
        if (col > 0) col++;
        i++;
        if (col >= cols && !nextRow()) return row;
        continue;
      }

      int wEnd = i;
      while (wEnd < lineEnd && s[wEnd] != ' ') wEnd++;
      const int wLen = wEnd - i;

      // Wrap before a word that doesn't fit here but would fit on a fresh
      // row. A word longer than a whole row is hard-broken below instead.
      if (col > 0 && col + wLen > cols && wLen <= cols) {
        if (!nextRow()) return row;
        continue;  // re-test on the new row
      }

      const NoteRole wordRole = noteWordRole(s, i, wEnd);
      while (i < wEnd) {
        if (col >= cols && !nextRow()) return row;  // hard break inside an over-long word
        NoteCell cell;
        cell.idx = i;
        cell.col = col;
        cell.row = row;
        if (s[i] == '`') {
          inCode = !inCode;
          cell.code = true;  // the tick itself counts, so a stray one is visible
        } else {
          cell.code = inCode;
        }
        cell.role = wordRole != NOTE_ROLE_NONE ? wordRole : ln.role;
        cell.marker = i == ln.marker;
        emit(ctx, cell);
        col++; i++;
      }
    }

    // End of source line (an empty line consumes a row, as an editor implies).
    i = lineEnd + 1;
    if (!nextRow()) return row;
  }
  return row;  // the row after the last glyph's == rows used
}
