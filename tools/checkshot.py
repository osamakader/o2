#!/usr/bin/env python3
# checkshot.py shot.ppm [splash|desk]: check a screen dump (a P6 PPM
# from the QEMU monitor's screendump) of gfx's test picture: the colored
# bars along the top and the circle in the middle; or with splash, of
# the boot animation's last frame: the o2 logo and the full bar under
# it; or with desk, of the desktop: a terminal window and the taskbar.
# Exits 0 if they are there.
import sys

data = open(sys.argv[1], "rb").read()
# header: P6, width, height, maxval, each followed by whitespace
fields, pos = [], 0
while len(fields) < 4:
    while data[pos:pos + 1].isspace():
        pos += 1
    start = pos
    while not data[pos:pos + 1].isspace():
        pos += 1
    fields.append(data[start:pos])
pos += 1
assert fields[0] == b"P6", "not a P6 PPM"
w, h = int(fields[1]), int(fields[2])
px = data[pos:]

def at(x, y):
    i = (y * w + x) * 3
    return tuple(px[i:i + 3])

if len(sys.argv) > 2 and sys.argv[2] == "splash":
    # where splash.c draws: units of height/5, centered a bit high
    unit = h / 5
    cx, cy = w / 2, h / 2 - unit * 0.3
    def pt(x, y):
        return at(int(cx + x * unit), int(cy + y * unit))
    checks = [
        ("the o's ring", pt(-0.53 + 0.30, 0.17), lambda p: p[2] > 200 and p[1] > 120),
        ("inside the o", pt(-0.53, 0.17), lambda p: max(p) < 90),
        ("the 2's base", pt(0.55, 0.47), lambda p: p[0] > 100 and p[2] > 200),
        ("the full bar", pt(0.75, 0.95), lambda p: p[2] > 180 and p[1] > 150),
    ]
    bad = 0
    for name, p, ok in checks:
        if not ok(p):
            print("%s: %s" % (name, p))
            bad += 1
    print("%dx%d: %s" % (w, h, "ok" if bad == 0 else "%d wrong" % bad))
    sys.exit(1 if bad else 0)

if len(sys.argv) > 2 and sys.argv[2] == "desk":
    # colors from desk.c; its first window is at (16, 16)
    checks = [
        ("the title bar", at(200, 26), (0x2f, 0x6f, 0xb8)),
        ("the terminal", at(300, 300), (0x10, 0x12, 0x14)),
        ("the taskbar", at(w // 2, h - 4), (0x1c, 0x1f, 0x24)),
        ("the desktop", at(w - 4, 4), None),
    ]
    bad = 0
    for name, got, want in checks:
        # the desktop: bluish, and nothing else is there
        ok = got[2] > got[0] + 32 and got[2] > 64 if want is None else got == want
        if not ok:
            print("%s: %s" % (name, got))
            bad += 1
    print("%dx%d: %s" % (w, h, "ok" if bad == 0 else "%d wrong" % bad))
    sys.exit(1 if bad else 0)

bars = [(255, 255, 255), (255, 0, 0), (0, 255, 0), (0, 0, 255),
        (255, 255, 0), (0, 255, 255), (255, 0, 255), (0, 0, 0)]
bad = 0
for i, color in enumerate(bars):
    got = at(i * (w // 8) + w // 16, h // 8)
    if got != color:
        print("bar %d: %s, not %s" % (i, got, color))
        bad += 1
got = at(w // 2, h * 5 // 8)
if got != (255, 160, 0):
    print("circle: %s, not (255, 160, 0)" % (got,))
    bad += 1
print("%dx%d: %s" % (w, h, "ok" if bad == 0 else "%d wrong" % bad))
sys.exit(1 if bad else 0)
