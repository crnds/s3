"""tools/note.py's tokenizer against the shared corpus (tests/note_tokens.json).

The same corpus runs against simulator-s3.html and note.html in
tools/note_corpus.mjs; this is the Python copy's half (roadmap O5).
"""
import importlib.util
import json
import os
import unittest

import helpers

CORPUS = json.load(open(os.path.join(helpers.REPO, "tests", "note_tokens.json")))
spec = importlib.util.spec_from_file_location("note_py", os.path.join(helpers.REPO, "tools", "note.py"))
note = importlib.util.module_from_spec(spec)
spec.loader.exec_module(note)

WORD_ROLES = {note.COL_WARN: "bad", note.COL_GOOD: "good", note.COL_YELLOW: "number"}
LINE_ROLES = {note.COL_ACCENT: "heading", note.COL_TEXT2: "quote"}


class NoteTokens(unittest.TestCase):
    def test_word_roles(self):
        for case in CORPUS["words"]:
            w = case["w"]
            colour = note.note_word_color(w, 0, len(w))
            self.assertEqual(WORD_ROLES.get(colour) if colour is not None else None, case["role"], repr(w))

    def test_line_context(self):
        for case in CORPUS["lines"]:
            s = case["s"]
            end, colour, marker = note.note_line_context(s, case["start"], len(s))
            label = repr(s)
            self.assertEqual(end, case["end"], label)
            self.assertEqual(LINE_ROLES.get(colour) if colour is not None else None, case["line"], label)
            self.assertEqual(marker, case["marker"], label)

    def test_every_colour_maps_to_a_role(self):
        # a colour that is neither a word nor a line role would be silently skipped above
        for case in CORPUS["words"]:
            c = note.note_word_color(case["w"], 0, len(case["w"]))
            self.assertTrue(c is None or c in WORD_ROLES, case["w"])


def nearest_256(rgb):
    """The xterm-256 index nearest to an RGB triple (colour cube + grays)."""
    levels = (0, 95, 135, 175, 215, 255)
    palette = {16 + 36 * r + 6 * g + b: (levels[r], levels[g], levels[b])
               for r in range(6) for g in range(6) for b in range(6)}
    palette.update({232 + i: (8 + 10 * i,) * 3 for i in range(24)})
    return min(palette, key=lambda k: sum((a - b) ** 2 for a, b in zip(palette[k], rgb)))


class NotePalette(unittest.TestCase):
    def test_yellow_is_the_firmware_note_number_colour(self):
        # tokens.h: TOK_COLOR_CONTENT_SUN, 0xFE60 = rgb(255,206,0). note.py once
        # still had the CYD's 0xFFE0 pure yellow (226).
        self.assertEqual(note.COL_YELLOW, nearest_256((255, 206, 0)))

    def test_palette_is_distinct(self):
        cols = [note.COL_TEXT2, note.COL_ACCENT, note.COL_GOOD, note.COL_WARN, note.COL_BLUE, note.COL_YELLOW]
        self.assertEqual(len(set(cols)), len(cols))


if __name__ == "__main__":
    unittest.main()
