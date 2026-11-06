"""Host-side preview of the e-ink boot screen defined in src/eink.c.

Re-implements text_hit()/screen_colour() and renders to PNG so layout
mistakes are caught before spending ~36 s on a real panel refresh.
"""
import re
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
W, H = 400, 300
src = (ROOT / "src" / "eink.c").read_text(encoding="utf-8")

# ---- pull the glyph table out of the C source -------------------------
glyphs = {}
for m in re.finditer(r"static const uint8_t (\w+)\[5\]\s*=\s*\{([^}]*)\}", src):
    name, body = m.group(1), m.group(2)
    vals = [int(v, 0) for v in body.split(",") if v.strip()]
    glyphs[name] = vals

# map char -> glyph name from the switch statement
charmap = {}
sw = re.search(r"switch \(c\) \{(.*?)\n    \}", src, re.S)
for m in re.finditer(r"case '([^']*)': return (\w+);", sw.group(1)):
    charmap[m.group(1)] = glyphs[m.group(2)]
for m in re.finditer(r"case '(\d)': return (\w+);", sw.group(1)):
    charmap[m.group(1)] = glyphs[m.group(2)]

SP = glyphs["sp"]


def glyph(c):
    return charmap.get(c, SP)


def text_hit(s, tx, ty, scale, x, y):
    cell = 6 * scale
    gx, gy = x - tx, y - ty
    if gx < 0 or gy < 0 or gy >= 7 * scale:
        return 0
    idx = gx // cell
    cx = gx - idx * cell
    if cx >= 5 * scale:
        return 0
    if idx >= len(s):
        return 0
    g = glyph(s[idx])
    return (g[cx // scale] >> (gy // scale)) & 1


def centre_x(s, scale):
    return (W - len(s) * 6 * scale) // 2


def screen_colour(x, y):
    frame = 3
    if x < frame or x >= W - frame or y < frame or y >= H - frame:
        return "BLACK"
    if y < 58:
        return "WHITE" if text_hit("EINK-N51802", 56, 18, 3, x, y) else "BLACK"
    if y >= 236:
        if text_hit("BLE READY", centre_x("BLE READY", 3), 250, 3, x, y):
            return "WHITE"
        return "RED"
    if text_hit("WFT0420CZ15", 24, 74, 2, x, y):
        return "BLACK"
    if text_hit("400X300 3-COLOUR", 24, 102, 2, x, y):
        return "BLACK"
    if text_hit("CTRL UC8176", 24, 130, 2, x, y):
        return "BLACK"
    if text_hit("FW 1.0", 24, 158, 2, x, y):
        return "BLACK"
    if text_hit("BLE EINK-N51802", 24, 186, 2, x, y):
        return "RED"
    if 224 <= y < 228:
        return "RED"
    return "WHITE"


PALETTE = {"WHITE": (255, 255, 255), "BLACK": (0, 0, 0), "RED": (200, 30, 30)}
img = Image.new("RGB", (W, H))
px = img.load()
for y in range(H):
    for x in range(W):
        px[x, y] = PALETTE[screen_colour(x, y)]
out = ROOT / "docs" / "boot_screen.png"
img.save(out)
print(f"wrote {out.relative_to(ROOT)}", img.size)
print("widgets:", len(charmap), "glyphs parsed")
