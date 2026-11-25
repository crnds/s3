"""TOK_LINE_CAPTION / TOK_LINE_BODY (src/tokens.h, sim/tokens.js) are the line
boxes of the 13px and 17px fonts. tools/make_vlw.py writes the real ones into
sim/fonts.js; this fails if the fonts change and the constants don't follow."""
import json
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def line_heights():
    text = (ROOT / "sim" / "fonts.js").read_text()
    doc = json.loads(text[text.index("{"):text.rindex("}") + 1])
    return {k: v["lineHeight"] for k, v in doc.items()}


def constants(path, pattern):
    text = (ROOT / path).read_text()
    return {n: int(v) for n, v in re.findall(pattern, text)}


class TypeMetrics(unittest.TestCase):
    def test_constants_match_fonts(self):
        lh = line_heights()
        fw = constants("src/tokens.h", r"int (TOK_LINE_\w+) = (\d+);")
        sim = constants("sim/tokens.js", r"(TOK_LINE_CAPTION|TOK_LINE_BODY) = (\d+)")
        sim.update(constants("sim/tokens.js", r", (TOK_LINE_BODY) = (\d+)"))
        want = {"TOK_LINE_CAPTION": lh["SM"], "TOK_LINE_BODY": lh["MD"]}
        self.assertEqual(lh["SM"], lh["SMB"])
        self.assertEqual(lh["MD"], lh["MDB"])
        self.assertEqual(fw, want)
        self.assertEqual(sim, want)


if __name__ == "__main__":
    unittest.main()
