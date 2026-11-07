"""Host-side preview of the e-ink boot screen defined in src/eink.c.

Re-implements text_hit()/screen_colour() for every supported panel and
renders a PNG, so layout mistakes are caught before spending 15-20 s on a
real panel refresh. Keep in sync with src/eink.c when the layout changes.

  python tools/preview_eink.py                # all layouts
  python tools/preview_eink.py hink-ssd1680   # just one
"""
import re
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
src = (ROOT / "src" / "eink.c").read_text(encoding="utf-8")

# (layout, panel name, controller, width, height)
LAYOUTS = {
    "hink-ssd1680": ("tall", "HINK-E029A01", "SSD1680", 128, 296),
    "hink-uc8151": ("tall", "HINK-E029A01", "UC8151", 128, 296),
    "hink-e0213a22": ("tall", "HINK-E0213A22-A0", "SSD1680", 122, 250),
    "wft-uc8176": ("wide", "WFT0420CZ15", "UC8176", 400, 300),
    "wft-uc8179": ("wide", "WFT0420CZ15", "UC8179", 400, 300),
}

# ---- pull the glyph table out of the C source -------------------------
glyphs = {}
for m in re.finditer(r"static const uint8_t (\w+)\[5\]\s*=\s*\{([^}]*)\}", src):
    name, body = m.group(1), m.group(2)
    glyphs[name] = [int(v, 0) for v in body.split(",") if v.strip()]

charmap = {}
sw = re.search(r"switch \(c\) \{(.*?)\n    \}", src, re.S)
for m in re.finditer(r"case '([^']*)': return (\w+);", sw.group(1)):
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


def centre(W, s, scale):
    return (W - len(s) * 6 * scale) // 2


def screen_wide(W, H, name, ctrl, x, y):
    frame = 3
    if x < frame or x >= W - frame or y < frame or y >= H - frame:
        return "BLACK"
    if y < 58:
        return "WHITE" if text_hit("EINK-N51802", 56, 20, 3, x, y) else "BLACK"
    if y >= 236:
        return "WHITE" if text_hit("BLE READY", centre(W, "BLE READY", 3), 250, 3, x, y) else "RED"
    if text_hit(name, 24, 74, 2, x, y):
        return "BLACK"
    if text_hit("400X300 3-COLOUR", 24, 102, 2, x, y):
        return "BLACK"
    if text_hit("CTRL", 24, 130, 2, x, y):
        return "BLACK"
    if text_hit(ctrl, 84, 130, 2, x, y):
        return "BLACK"
    if text_hit("FW 2.0", 24, 158, 2, x, y):
        return "BLACK"
    if text_hit("BLE EINK-N51802", 24, 186, 2, x, y):
        return "RED"
    if 224 <= y < 228:
        return "RED"
    return "WHITE"


def screen_tall(W, H, name, ctrl, x, y):
    frame = 3

    def ys(v):
        return v * H // 296

    if x < frame or x >= W - frame or y < frame or y >= H - frame:
        return "BLACK"
    if y < ys(52):
        if text_hit("EINK-N51802", centre(W, "EINK-N51802", 1), ys(12), 1, x, y):
            return "WHITE"
        if text_hit(name, centre(W, name, 1), ys(30), 1, x, y):
            return "WHITE"
        return "BLACK"
    if y >= ys(246):
        return "WHITE" if text_hit("EINK TAG", centre(W, "EINK TAG", 2), ys(262), 2, x, y) else "RED"
    if text_hit("3-COLOUR", 6, ys(66), 1, x, y):
        return "BLACK"
    if text_hit("CTRL", 6, ys(86), 1, x, y):
        return "BLACK"
    if text_hit(ctrl, 36, ys(86), 1, x, y):
        return "BLACK"
    if text_hit("FW 2.0", 6, ys(106), 1, x, y):
        return "BLACK"
    if text_hit("BLE 57600", 6, ys(126), 1, x, y):
        return "BLACK"
    if ys(152) <= y < ys(156):
        return "RED"
    if text_hit("BLE READY", 6, ys(168), 2, x, y):
        return "RED"
    if text_hit("PAIR WITH", 6, ys(210), 1, x, y):
        return "BLACK"
    if text_hit("EINK-N51802", 6, ys(228), 1, x, y):
        return "BLACK"
    return "WHITE"


PALETTE = {"WHITE": (255, 255, 255), "BLACK": (0, 0, 0), "RED": (200, 30, 30)}


def render(key):
    layout, name, ctrl, W, H = LAYOUTS[key]
    fn = screen_wide if layout == "wide" else screen_tall

    img = Image.new("RGB", (W, H))
    px = img.load()
    for y in range(H):
        for x in range(W):
            px[x, y] = PALETTE[fn(W, H, name, ctrl, x, y)]

    out = ROOT / "docs" / f"boot_screen_{key}.png"
    img.save(out)
    print(f"wrote {out.relative_to(ROOT)} {img.size} ({name} / {ctrl})")


if __name__ == "__main__":
    keys = sys.argv[1:] or list(LAYOUTS)
    for k in keys:
        if k not in LAYOUTS:
            sys.exit(f"unknown layout {k!r}; choose from {', '.join(LAYOUTS)}")
        render(k)
    print(f"widgets: {len(charmap)} glyphs parsed")
