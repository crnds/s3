#!/usr/bin/env python3
"""Board-vs-simulator parity, in one command (roadmap O7, board half).

Loads tests/fixtures/<name>.json onto the board over USB serial (the 'F'
command: canned usage data, a frozen wall clock, the theme, polling off), grabs
every page and sheet in both themes with 'g', and compares each frame with the
simulator's golden (tests/golden/<name>/<screen>-<theme>.png, which CI already
keeps equal to simulator-s3.html). Writes a diff PNG per screen and a report to
tests/out/parity/, prints a table, and exits 1 if a screen has more differing
pixels than its baseline.

    ~/.pio-venv/bin/python tools/parity.py                    # compare, both themes
    ~/.pio-venv/bin/python tools/parity.py --themes dark      # one theme
    ~/.pio-venv/bin/python tools/parity.py --save-baseline    # record today's counts

Needs pyserial (the PlatformIO venv has it; the system python3 does not) and
the board on USB with a build that has the 'F' command. 'f' (sent at the end)
resumes polling and restores the board's saved theme.

What differs by design, and how it is handled:
  - Pages 2, 3, 4 and 6 play cats/movies (non-twins, CLAUDE.md): the simulator
    shows a placeholder where the board shows a live clip. They are reported but
    never fail the run.
  - The board's own settings (brightness, Show AQI, ...) are not the
    simulator's defaults: a screen that differs everywhere usually means one of
    those, not a drawing bug. The fixture's "settings" block is ignored here.
  - Animated bits (the status dot's breath, the pace sweep every 3 s) and the
    live CPU readout differ between frames; the baseline absorbs them.
  - --tol 8 (default) hides RGB565 expansion rounding. Lower it to see anti-
    aliasing blend differences too (the board blends in RGB565, the simulator
    in RGB888).
"""
import argparse
import glob
import json
import os
import struct
import sys
import time
import zlib
from datetime import datetime

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

try:
    import serial
except ImportError:
    print("Error: pyserial missing -- run with ~/.pio-venv/bin/python (pip install pyserial)")
    sys.exit(1)

from grab_screen import read_until  # noqa: E402

W, H = 480, 320
# screen -> serial keys that get there from page 0 (None = report only, below)
PAGES = ["page%d" % i for i in range(7)]
SHEETS = {"weather": "w", "device": "d", "settings": "s"}
REPORT_ONLY = {"page2", "page3", "page4", "page6",  # live cat/movie content on the board
               "device"}                           # live hardware numbers (the simulator shows constants)
# Rects (x0, y0, x1, y1) ignored on every screen: the status strip's live group
# -- the dot's breath, the WiFi bars and the CPU readout -- which the frozen
# simulator cannot match frame for frame.
MASKS = [(8, 298, 112, 319)]
SETTLE = 1.4  # seconds after a navigation key: the slide/sheet spring must land


# ── PNG (stdlib only) ──────────────────────────────────────────────────────

def read_png(path):
    """8-bit, non-interlaced RGB/RGBA PNG -> (w, h, bytes RGB)."""
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w = 8, b"", 0
    while pos < len(data):
        n, kind = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and ctype in (2, 6) and interlace == 0, "unsupported PNG: " + path
        elif kind == b"IDAT":
            idat += body
        pos += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw = zlib.decompress(idat)
    stride = w * bpp
    out = bytearray(h * stride)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 255
        elif f == 2:
            for i in range(stride):
                line[i] = (line[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((a + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[i] = (line[i] + pred) & 255
        out[y * stride:(y + 1) * stride] = line
        prev = line
    if bpp == 4:  # drop alpha (the canvas is opaque)
        rgb = bytearray(w * h * 3)
        rgb[0::3], rgb[1::3], rgb[2::3] = out[0::4], out[1::4], out[2::4]
        return w, h, bytes(rgb)
    return w, h, bytes(out)


def write_png(path, w, h, rgb):
    def chunk(kind, body):
        c = struct.pack(">I", len(body)) + kind + body
        return c + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


# ── board ───────────────────────────────────────────────────────────────────

def grab_frame(ser):
    """The board's frame as RGB bytes (big-endian RGB565 on the wire)."""
    ser.reset_input_buffer()
    ser.write(b"g")
    _, rest = read_until(ser, b"S3SHOT ", 5)
    while b"\n" not in rest:
        rest += ser.read(64)
    header, data = rest.split(b"\n", 1)
    w, h = map(int, header.split())
    need = w * h * 2
    end = time.time() + 20
    while len(data) < need and time.time() < end:
        data += ser.read(need - len(data))
    if len(data) < need:
        raise TimeoutError("short frame: %d/%d bytes" % (len(data), need))
    rgb = bytearray(w * h * 3)
    for i in range(w * h):
        v = (data[2 * i] << 8) | data[2 * i + 1]
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        rgb[3 * i], rgb[3 * i + 1], rgb[3 * i + 2] = (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)
    return w, h, bytes(rgb)


def load_fixture(ser, fixture, light):
    epoch = int(datetime.fromisoformat(fixture["clock"]).timestamp())
    body = json.dumps(fixture["usage"], separators=(",", ":")).encode()
    ser.reset_input_buffer()
    ser.write(b"F%d %d\n" % (epoch, 1 if light else 0))
    try:  # the board may be mid-render; its USB receive buffer is only 256 bytes
        read_until(ser, b"[fixture] send", 5)
    except TimeoutError:
        raise SystemExit("board did not answer '[fixture] send' -- is this build new enough (serial 'F')?")
    for i in range(0, len(body), 128):
        ser.write(body[i:i + 128])
        ser.flush()
        time.sleep(0.01)
    ser.write(b"\n")
    try:
        read_until(ser, b"[fixture] on", 8)
    except TimeoutError:
        raise SystemExit("board did not answer '[fixture] on' -- is this build new enough (serial 'F')?")
    time.sleep(SETTLE)


def key(ser, k, settle=SETTLE):
    ser.write(k.encode())
    ser.flush()
    time.sleep(settle)


def capture(port, fixture, themes):
    """{(screen, theme): (w, h, rgb)} for every page and sheet."""
    ser = serial.Serial(port, 115200, timeout=0.2)
    time.sleep(1.0)
    shots = {}
    try:
        for theme in themes:
            key(ser, "x", 0.3)
            load_fixture(ser, fixture, theme == "light")
            for i, name in enumerate(PAGES):
                if i:
                    key(ser, "n")
                shots[(name, theme)] = grab_frame(ser)
                print("  grabbed %s-%s" % (name, theme), flush=True)
            key(ser, "n")  # page 6 -> 0
            for name, k in SHEETS.items():
                key(ser, k)
                shots[(name, theme)] = grab_frame(ser)
                print("  grabbed %s-%s" % (name, theme), flush=True)
                key(ser, "x")
    finally:
        ser.write(b"f")  # polling back on
        ser.close()
    return shots


# ── compare ─────────────────────────────────────────────────────────────────

def compare(board, golden, tol):
    """(differing pixel count, bbox or None, diff RGB bytes)."""
    n, x0, y0, x1, y1 = 0, W, H, -1, -1
    diff = bytearray(len(board))
    for i in range(W * H):
        j = 3 * i
        x, y = i % W, i // W
        if any(a <= x <= c and b <= y <= d for a, b, c, d in MASKS):
            diff[j], diff[j + 1], diff[j + 2] = 0, 0, 40  # masked: dark blue
            continue
        br, bg, bb = board[j], board[j + 1], board[j + 2]
        d = max(abs(br - golden[j]), abs(bg - golden[j + 1]), abs(bb - golden[j + 2]))
        if d > tol:
            n += 1
            x0, y0, x1, y1 = min(x0, x), min(y0, y), max(x1, x), max(y1, y)
            diff[j], diff[j + 1], diff[j + 2] = 255, 0, 64
        else:  # dimmed board pixel, so the picture still reads
            diff[j], diff[j + 1], diff[j + 2] = br >> 2, bg >> 2, bb >> 2
    return n, ((x0, y0, x1, y1) if n else None), bytes(diff)


def main():
    ap = argparse.ArgumentParser(description="Compare the board's screens with the simulator goldens.")
    ap.add_argument("--fixture", default="default", help="tests/fixtures/<name>.json (default: default)")
    ap.add_argument("--themes", default="dark,light")
    ap.add_argument("--port", help="default: the only /dev/cu.usbmodem* present")
    ap.add_argument("--tol", type=int, default=8,
                    help="ignore channel differences up to this (default 8: one RGB565 step; the board expands\n                    565 to 888 and the goldens are not 565-exact, so most pixels differ by 1)")
    ap.add_argument("--baseline", default=os.path.join(ROOT, "tests", "parity_baseline.json"))
    ap.add_argument("--slack", type=float, default=0.35,
                    help="allowed growth over the baseline, as a fraction, plus 25 px (the pace sweep moves between runs:\n                    page 0 has varied 755-1026 px at a baseline of ~800; a layout break is thousands)")
    ap.add_argument("--save-baseline", action="store_true", help="record this run's counts as the baseline")
    ap.add_argument("--outdir", default=os.path.join(ROOT, "tests", "out", "parity"))
    args = ap.parse_args()

    port = args.port
    if not port:
        ports = sorted(glob.glob("/dev/cu.usbmodem*"))
        if len(ports) != 1:
            sys.exit("Error: " + ("no /dev/cu.usbmodem* port -- is the board plugged in?" if not ports
                                  else "several ports (%s) -- pick one with --port" % ", ".join(ports)))
        port = ports[0]
    themes = args.themes.split(",")
    fixture = json.load(open(os.path.join(ROOT, "tests", "fixtures", args.fixture + ".json")))
    gdir = os.path.join(ROOT, "tests", "golden", args.fixture)
    os.makedirs(args.outdir, exist_ok=True)

    print("capturing from %s ..." % port)
    shots = capture(port, fixture, themes)

    baseline = {}
    if os.path.exists(args.baseline) and not args.save_baseline:
        baseline = json.load(open(args.baseline))
    results, failed = {}, False
    print("\n%-16s %9s %7s  %-18s %s" % ("screen", "diff px", "%", "bbox", "status"))
    for (name, theme), (w, h, rgb) in sorted(shots.items(), key=lambda kv: (kv[0][1], (PAGES + list(SHEETS)).index(kv[0][0]))):
        gw, gh, golden = read_png(os.path.join(gdir, "%s-%s.png" % (name, theme)))
        assert (w, h) == (gw, gh) == (W, H), "size mismatch for %s-%s" % (name, theme)
        n, bbox, diff = compare(rgb, golden, args.tol)
        label = "%s-%s" % (name, theme)
        write_png(os.path.join(args.outdir, label + ".diff.png"), W, H, diff)
        write_png(os.path.join(args.outdir, label + ".board.png"), W, H, rgb)
        results[label] = n
        base = baseline.get(label, 0)
        allowed = int(base * (1 + args.slack)) + 25 if label in baseline else 0
        if name in REPORT_ONLY:
            status = "report only (live media)"
        elif n > allowed:
            status, failed = "FAIL (baseline %d, allowed %d)" % (base, allowed), True
        else:
            status = "ok" if n == 0 else "ok (baseline %d)" % base
        print("%-16s %9d %6.2f%%  %-18s %s" % (label, n, 100.0 * n / (W * H), bbox or "-", status))
    with open(os.path.join(args.outdir, "report.json"), "w") as f:
        json.dump({"fixture": args.fixture, "tol": args.tol, "diff_px": results}, f, indent=1, sort_keys=True)
    if args.save_baseline:
        twin = {k: v for k, v in results.items() if k.split("-")[0] not in REPORT_ONLY}
        with open(args.baseline, "w") as f:
            json.dump(twin, f, indent=1, sort_keys=True)
            f.write("\n")
        print("\nbaseline saved to %s" % args.baseline)
    print("\ndiff and board PNGs: %s" % args.outdir)
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
