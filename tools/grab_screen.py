#!/usr/bin/env python3
"""Drive the board over USB serial and save screenshots of its framebuffer.

The firmware answers single-key commands (see main.cpp's serialCommand):
n/p next/previous page, w/d/s Weather/Device/Settings, x close overlays,
g dump the 480x320 frame. This script sends a sequence of them and writes a
PNG for every 'g'.

Needs pyserial + Pillow (the system python3 here has both):
    python3 tools/grab_screen.py g   (needs pyserial + Pillow)
    python3 tools/grab_screen.py --port /dev/cu.usbmodem1101 "g n g n g"

Commands are space-separated; 'g' may be written 'g:name' to pick the file
name (shots/<name>.png). A bare number is a pause in seconds.
"""
import argparse
import os
import struct
import sys
import time

try:
    import serial
except ImportError:
    print("Error: pyserial missing -- pip install pyserial")
    sys.exit(1)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def read_until(ser, marker, timeout):
    buf = b""
    end = time.time() + timeout
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            buf += chunk
            i = buf.find(marker)
            if i >= 0:
                return buf[:i], buf[i + len(marker):]
    raise TimeoutError(f"no {marker!r} within {timeout}s")


def grab(ser, path):
    ser.reset_input_buffer()
    ser.write(b"g")
    _, rest = read_until(ser, b"S3SHOT ", 5)
    while b"\n" not in rest:
        rest += ser.read(64)
    header, rest = rest.split(b"\n", 1)
    w, h = map(int, header.split())
    need = w * h * 2
    data = rest
    end = time.time() + 20
    while len(data) < need and time.time() < end:
        data += ser.read(need - len(data))
    if len(data) < need:
        raise TimeoutError(f"short frame: {len(data)}/{need} bytes")
    raw = data[:need]
    px = bytearray(w * h * 3)
    for i in range(w * h):
        v = (raw[2 * i] << 8) | raw[2 * i + 1]  # big-endian RGB565
        r, g, b = (v >> 11) & 0x1F, (v >> 5) & 0x3F, v & 0x1F
        px[3 * i] = (r << 3) | (r >> 2)
        px[3 * i + 1] = (g << 2) | (g >> 4)
        px[3 * i + 2] = (b << 3) | (b >> 2)
    try:
        from PIL import Image
        Image.frombytes("RGB", (w, h), bytes(px)).save(path)
    except ImportError:  # fall back to a plain PPM next to the requested name
        path = os.path.splitext(path)[0] + ".ppm"
        with open(path, "wb") as fh:
            fh.write(b"P6\n%d %d\n255\n" % (w, h) + bytes(px))
    print(f"saved {path}")


def main():
    ap = argparse.ArgumentParser(description="Screenshot the S3 dashboard over serial.")
    ap.add_argument("commands", nargs="*", default=["g"], help='e.g. "g n g w g x" (default: g)')
    ap.add_argument("--port", default="/dev/cu.usbmodem1101")
    ap.add_argument("--outdir", default=os.path.join(ROOT, "shots"))
    ap.add_argument("--settle", type=float, default=0.8, help="seconds to wait after each navigation key")
    args = ap.parse_args()
    os.makedirs(args.outdir, exist_ok=True)
    cmds = " ".join(args.commands).split()
    ser = serial.Serial(args.port, 115200, timeout=0.2)
    n = 0
    for c in cmds:
        if c[0] == "g":
            name = c.split(":", 1)[1] if ":" in c else f"shot_{n:02d}"
            grab(ser, os.path.join(args.outdir, name + ".png"))
            n += 1
        elif c.replace(".", "", 1).isdigit():
            time.sleep(float(c))
        else:
            ser.write(c.encode())
            time.sleep(args.settle)
    ser.close()


if __name__ == "__main__":
    main()
