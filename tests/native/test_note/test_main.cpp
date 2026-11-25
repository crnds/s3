// note_pure.cpp against tests/note_tokens.json, the corpus the simulator, note.html and
// tools/note.py run too. The corpus is read as the file it is (pio test runs from the
// repo root); the parser handles exactly the shapes it contains: one JSON object per
// line, string values with \" and \\ escapes.
#include <unity.h>

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#include "note_pure.h"

void setUp(void) {}
void tearDown(void) {}

static std::string readAll(const char* path) {
  FILE* f = fopen(path, "rb");
  TEST_ASSERT_NOT_NULL_MESSAGE(f, "tests/note_tokens.json not found (run pio test from the repo root)");
  std::string s;
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
  fclose(f);
  return s;
}

// The string value of "key" in obj, unescaped; found=false for null / missing.
static std::string strField(const std::string& obj, const char* key, bool& found) {
  std::string k = std::string("\"") + key + "\": ";
  size_t p = obj.find(k);
  found = false;
  if (p == std::string::npos) return "";
  p += k.size();
  if (obj.compare(p, 4, "null") == 0) return "";
  if (obj[p] != '"') return "";
  found = true;
  std::string out;
  for (++p; p < obj.size() && obj[p] != '"'; ++p) {
    if (obj[p] == '\\' && p + 1 < obj.size()) {
      ++p;
      char c = obj[p];
      out += c == 'n' ? '\n' : c;
    } else out += obj[p];
  }
  return out;
}

static long intField(const std::string& obj, const char* key) {
  std::string k = std::string("\"") + key + "\": ";
  size_t p = obj.find(k);
  TEST_ASSERT_TRUE(p != std::string::npos);
  return strtol(obj.c_str() + p + k.size(), nullptr, 10);
}

// The objects of the array named `name` (each a single line).
static std::vector<std::string> objects(const std::string& doc, const char* name) {
  std::vector<std::string> v;
  size_t a = doc.find(std::string("\"") + name + "\": [");
  TEST_ASSERT_TRUE(a != std::string::npos);
  size_t end = doc.find("\n  ]", a);
  size_t p = doc.find('\n', a);
  while (p < end) {
    size_t q = doc.find('\n', p + 1);
    if (q > end) q = end;
    std::string line = doc.substr(p + 1, q - p - 1);
    if (line.find('{') != std::string::npos) v.push_back(line);
    p = q;
  }
  return v;
}

static NoteRole roleOf(const std::string& name, bool found) {
  if (!found) return NOTE_ROLE_NONE;
  if (name == "bad") return NOTE_ROLE_BAD;
  if (name == "good") return NOTE_ROLE_GOOD;
  if (name == "number") return NOTE_ROLE_NUMBER;
  if (name == "heading") return NOTE_ROLE_HEADING;
  if (name == "quote") return NOTE_ROLE_QUOTE;
  TEST_FAIL_MESSAGE(("unknown role " + name).c_str());
  return NOTE_ROLE_NONE;
}

void test_word_corpus() {
  std::string doc = readAll("tests/note_tokens.json");
  auto cases = objects(doc, "words");
  TEST_ASSERT_GREATER_THAN(40, (int)cases.size());
  for (auto& c : cases) {
    bool fw, fr;
    std::string w = strField(c, "w", fw), r = strField(c, "role", fr);
    NoteRole want = roleOf(r, fr);
    NoteRole got = noteWordRole(w.c_str(), 0, (int)w.size());
    char msg[160];
    snprintf(msg, sizeof msg, "word '%s'", w.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE(want, got, msg);
  }
}

void test_line_corpus() {
  std::string doc = readAll("tests/note_tokens.json");
  auto cases = objects(doc, "lines");
  TEST_ASSERT_GREATER_THAN(15, (int)cases.size());
  for (auto& c : cases) {
    bool f, fl;
    std::string s = strField(c, "s", f), line = strField(c, "line", fl);
    int start = (int)intField(c, "start");
    NoteLine got = noteLineContext(s.c_str(), start, (int)s.size());
    char msg[160];
    snprintf(msg, sizeof msg, "line '%s'", s.c_str());
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)intField(c, "end"), got.end, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE(roleOf(line, fl), got.role, msg);
    TEST_ASSERT_EQUAL_INT_MESSAGE((int)intField(c, "marker"), got.marker, msg);
  }
}


// ── layout (noteWrap): render the placed glyphs back into rows of text ──
struct Grid {
  std::vector<std::string> rows;
  std::vector<NoteCell> cells;
};
static void gridEmit(void* ctx, const NoteCell& c) {
  Grid& g = *static_cast<Grid*>(ctx);
  if ((int)g.rows.size() <= c.row) g.rows.resize(c.row + 1);
  std::string& r = g.rows[c.row];
  if ((int)r.size() <= c.col) r.resize(c.col + 1, '.');
  g.cells.push_back(c);
}
static std::string wrapped(const char* s, int cols, int maxRows, int* used = nullptr, Grid* keep = nullptr) {
  Grid g;
  int n = noteWrap(s, (int)strlen(s), cols, maxRows, gridEmit, &g);
  for (auto& c : g.cells) g.rows[c.row][c.col] = s[c.idx];
  if (used) *used = n;
  if (keep) *keep = g;
  std::string out;
  for (size_t i = 0; i < g.rows.size(); i++) out += (i ? "|" : "") + g.rows[i];
  return out;
}

void test_wrap_word_boundary() {
  int rows;
  TEST_ASSERT_EQUAL_STRING("aaa.bb|cc", wrapped("aaa bb cc", 6, 9, &rows).c_str());
  TEST_ASSERT_EQUAL_INT(2, rows);
}
void test_wrap_hard_break_long_word() {
  TEST_ASSERT_EQUAL_STRING("abcd|efgh|ij", wrapped("abcdefghij", 4, 9).c_str());
}
void test_wrap_empty_line_takes_a_row() {
  int rows;
  TEST_ASSERT_EQUAL_STRING("a||b", wrapped("a\n\nb", 8, 9, &rows).c_str());
  TEST_ASSERT_EQUAL_INT(3, rows);
}
void test_wrap_indent_collapses() {
  TEST_ASSERT_EQUAL_STRING("a...b", wrapped("a   b", 8, 9).c_str());  // inner spaces keep their width
  TEST_ASSERT_EQUAL_STRING("x", wrapped("   x", 8, 9).c_str());
}
void test_wrap_clips_at_max_rows() {
  int rows;
  TEST_ASSERT_EQUAL_STRING("ab|cd", wrapped("abcdefgh", 2, 2, &rows).c_str());
  TEST_ASSERT_EQUAL_INT(2, rows);
  // The first row is always allowed.
  TEST_ASSERT_EQUAL_STRING("ab", wrapped("abcd", 2, 0).c_str());
}
void test_wrap_roles_and_code() {
  Grid g;
  wrapped("# hi `x` TODO\n- it", 20, 9, nullptr, &g);
  // '# hi' is a heading line; the backticked x is code even on that line;
  // TODO is a word role over the heading's line role; the list marker is flagged.
  for (auto& c : g.cells) {
    if (c.row == 0 && c.col <= 3) { TEST_ASSERT_EQUAL_INT(NOTE_ROLE_HEADING, c.role); TEST_ASSERT_FALSE(c.code); }
    if (c.row == 0 && c.col >= 5 && c.col <= 7) TEST_ASSERT_TRUE(c.code);
    if (c.row == 0 && c.col >= 9) TEST_ASSERT_EQUAL_INT(NOTE_ROLE_BAD, c.role);
    if (c.row == 1 && c.col == 0) TEST_ASSERT_TRUE(c.marker);
    if (c.row == 1 && c.col > 0) TEST_ASSERT_FALSE(c.marker);
  }
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_word_corpus);
  RUN_TEST(test_line_corpus);
  RUN_TEST(test_wrap_word_boundary);
  RUN_TEST(test_wrap_hard_break_long_word);
  RUN_TEST(test_wrap_empty_line_takes_a_row);
  RUN_TEST(test_wrap_indent_collapses);
  RUN_TEST(test_wrap_clips_at_max_rows);
  RUN_TEST(test_wrap_roles_and_code);
  return UNITY_END();
}
