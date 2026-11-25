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


if __name__ == "__main__":
    unittest.main()
