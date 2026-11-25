"""The note's round trip through ~/.s3_note.json (a temp file here)."""
import json
import os
import tempfile
import unittest
from unittest import mock

import helpers  # noqa: F401  (puts server/ on sys.path)
from s3srv import notes


class NoteFile(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.path = os.path.join(self.tmp.name, "note.json")
        p = mock.patch.object(notes, "NOTE_PATH", self.path)
        p.start()
        self.addCleanup(p.stop)
        saved = notes.state_get("note")
        self.addCleanup(notes.state_set, "note", saved)
        notes.state_set("note", {"text": "", "size": 1})

    def write(self, raw):
        with open(self.path, "w") as f:
            f.write(raw)

    def test_set_note_round_trip(self):
        notes.set_note({"text": "buy milk", "size": 2})
        self.assertEqual(notes.note_snapshot(), {"text": "buy milk", "size": 2})
        notes.state_set("note", {"text": "", "size": 1})
        notes.load_note()
        self.assertEqual(notes.note_snapshot(), {"text": "buy milk", "size": 2})
        self.assertFalse(os.path.exists(self.path + ".tmp"))

    def test_load_sanitizes_and_clamps(self):
        self.write(json.dumps({"text": "a\tbก", "size": 9}))
        notes.load_note()
        self.assertEqual(notes.note_snapshot(), {"text": "a b?", "size": 3})

    def test_missing_or_bad_file_keeps_the_empty_note(self):
        for raw in (None, "{not json", "[1, 2]", '{"size": "big"}'):
            if raw is not None:
                self.write(raw)
            notes.load_note()
            self.assertEqual(notes.note_snapshot(), {"text": "", "size": 1}, raw)

    def test_snapshot_is_a_copy(self):
        notes.note_snapshot()["text"] = "changed"
        self.assertEqual(notes.note_snapshot()["text"], "")


if __name__ == "__main__":
    unittest.main()
