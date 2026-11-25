// Note tokenizer, pure part (no Arduino, no colours): word and line classification.
// The rules are duplicated in sim/page_note.js, note.html and tools/note.py; all four
// run tests/note_tokens.json. Colours are chosen at draw time (page_note.cpp).
#pragma once

enum NoteRole : unsigned char {
  NOTE_ROLE_NONE = 0,   // no override: the line's colour or note.text applies
  NOTE_ROLE_BAD,        // TODO / FIXME / BUG -> note.keyword.bad
  NOTE_ROLE_GOOD,       // DONE / OK          -> note.keyword.good
  NOTE_ROLE_NUMBER,     // numeric token      -> note.number
  NOTE_ROLE_HEADING,    // line starts '#'    -> note.heading (whole line)
  NOTE_ROLE_QUOTE,      // line starts '>'    -> note.quote   (whole line)
};

// Role of the word s[a..b): wrapping punctuation is trimmed for the test only.
NoteRole noteWordRole(const char* s, int a, int b);

struct NoteLine {
  int end;          // index of the line's '\n' (or n)
  NoteRole role;    // NOTE_ROLE_HEADING, NOTE_ROLE_QUOTE or NOTE_ROLE_NONE
  int marker;       // index of a leading "- ", "* ", "+ " marker char, or -1
};
// Classify the source line starting at s[i] (s has n characters).
NoteLine noteLineContext(const char* s, int i, int n);

// ── Layout: where each glyph of the note goes ──
// One placed glyph. `role` is the word's role, else the line's, else NONE;
// `code` (inside backticks, the ticks included) wins over both, and `marker`
// is the line's leading list marker. The draw side maps these to colours.
struct NoteCell {
  int idx;          // index into the note text
  int col, row;     // glyph grid position
  NoteRole role;
  bool code;
  bool marker;
};
typedef void (*NoteCellFn)(void* ctx, const NoteCell& cell);
// Word-wrap s[0..n) onto a grid `cols` wide, at most `maxRows` rows (the first
// row is always allowed); the rest is clipped. Calls emit for each glyph in
// reading order; spaces place nothing. Returns the rows used.
int noteWrap(const char* s, int n, int cols, int maxRows, NoteCellFn emit, void* ctx);
