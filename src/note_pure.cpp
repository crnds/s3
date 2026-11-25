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
