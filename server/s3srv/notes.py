"""The Note page: text from note.html / tools/note.py, folded to what the
board can draw, kept in STATE and in ~/.s3_note.json."""

import json
import os

from s3srv.common import REPO_DIR, USAGE_PORT, log_err
from s3srv.state import state_get, state_set

# ── Note page (board page 6) ──────────────────────────────────────────────
# Free text authored in note.html and rendered in the right-hand pane of the
# board's Note page. It rides along inside /api/usage rather than getting its
# own endpoint, so the board needs no second HTTP call and gets SD caching for
# free via the existing /last_usage.json replay.
NOTE_PATH = os.path.expanduser("~/.s3_note.json")
# The board's pane draws at most 24 cols x 19 rows = 456 glyphs at text size 1.
# 480 sits just above that ceiling (headroom for newlines) and comfortably
# under the firmware's 512-byte buffer. Because sanitize_note() forces ASCII,
# characters == bytes and the two caps are directly comparable.
NOTE_MAX_CHARS = 480
# The editor page sits at the repo root (note.html), next to the control
# panel's server.html.
NOTE_HTML_PATH = os.path.join(REPO_DIR, "note.html")
# note.html is served from this same origin, so its POST carries an Origin
# header — the blanket "reject any Origin" rule /api/battery-save uses (see
# do_POST) can't be reused here. Allowlist the origins this server can
# legitimately be reached on instead, plus "null" for the file:// dev case.
NOTE_ALLOWED_ORIGINS = {
    f"http://127.0.0.1:{USAGE_PORT}",
    f"http://localhost:{USAGE_PORT}",
    "null",
}
def sanitize_note(text):
    """Fold arbitrary editor text down to what the board can actually draw.

    The CYD renders with the built-in Adafruit GFX font, which only has glyphs
    for 7-bit printable ASCII. Thai, emoji and box-drawing characters have no
    glyph at all, so passing them through would paint garbage. They become "?"
    rather than being dropped, so a note never silently loses content — and
    note.html warns about the count before you ever get here.
    """
    if not isinstance(text, str):
        return ""
    text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\t", " ")
    out = []
    for ch in text:
        if ch == "\n" or " " <= ch <= "~":
            out.append(ch)
        else:
            out.append("?")
    return "".join(out)[:NOTE_MAX_CHARS]


def note_snapshot():
    return dict(state_get("note"))


def load_note():
    """Seed STATE["note"] from disk at startup. A missing/corrupt file is not
    an error — it just means "no note yet"."""
    try:
        with open(NOTE_PATH, "r", encoding="utf-8") as f:
            doc = json.load(f)
        note = {
            "text": sanitize_note(doc.get("text", "")),
            "size": min(3, max(1, int(doc.get("size", 1)))),
        }
    except (OSError, ValueError, TypeError, AttributeError):  # AttributeError: a non-object file
        return
    state_set("note", note)


def save_note(note):
    """Persist atomically so a crash mid-write can't leave a truncated file
    that load_note() would then discard on the next boot."""
    tmp = NOTE_PATH + ".tmp"
    try:
        with open(tmp, "w", encoding="utf-8") as f:
            json.dump(note, f)
        os.replace(tmp, NOTE_PATH)
    except OSError as e:
        log_err(f"save_note: {type(e).__name__}: {e}")


def set_note(note):
    """POST /api/note: what the board shows from its next poll, kept on disk."""
    state_set("note", note)
    save_note(note)
