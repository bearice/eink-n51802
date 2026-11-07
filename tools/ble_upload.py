"""Upload a test image to EINK-N51802 over BLE and trigger a refresh.

Renders an image at the active panel's resolution, converts it into the
panel's two bit planes (black and red), and streams them to
characteristic FFF1 using the firmware's packet protocol:

    0x01 | begin (panel reset+init, black plane open)
    0x02 | <up to 19 image bytes>
    0x03 | switch to the red plane
    0x04 | end -> refresh

Plane encoding (logical; the firmware re-encodes it per panel):
    black plane bit 1 = no black
    red plane   bit 1 = red

Geometry comes from the panel the firmware is configured for: 128x296
for the 2.9" HINK-E029A01 (default) and 400x300 for the 4.2"
WFT0420CZ15. Pass --panel wft for the latter.
"""
import argparse
import asyncio
import sys
from pathlib import Path

from bleak import BleakClient, BleakScanner
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
CHR = "0000fff1-0000-1000-8000-00805f9b34fb"
CFG_CHR = "0000fff2-0000-1000-8000-00805f9b34fb"
CHUNK = 19
OP_BEGIN, OP_DATA, OP_RED, OP_END = 0x01, 0x02, 0x03, 0x04

PANELS = {
    "hink": (128, 296),     # HINK-E029A01 2.9" (SSD1680 / UC8151)
    "hink213": (122, 250),  # HINK-E0213A22-A0 2.13" (SSD1680)
    "wft": (400, 300),      # WFT0420CZ15 4.2" (UC8176 / UC8179)
}


def _font(size):
    try:
        return ImageFont.load_default(size=size)
    except TypeError:
        return ImageFont.load_default()


def _centre_text(d, box, text, fill, font):
    """Draw `text` centred inside (x0, y0, x1, y1)."""
    x0, y0, x1, y1 = box
    bb = d.textbbox((0, 0), text, font=font)
    d.text((x0 + (x1 - x0 - (bb[2] - bb[0])) / 2 - bb[0],
            y0 + (y1 - y0 - (bb[3] - bb[1])) / 2 - bb[1]),
           text, fill=fill, font=font)


def make_image(w, h):
    """Test card laid out for the panel's own orientation.

    Portrait (2.9" 128x296): red header band, a black box and a red ellipse
    side by side, a rule and a caption. Landscape (4.2" 400x300): the
    original wide layout. Nothing may overlap the other element - the
    point of the card is to show whether geometry and colours are right.
    """
    img = Image.new("RGB", (w, h), "white")
    d = ImageDraw.Draw(img)
    bw = max(2, min(w, h) // 50)                  # border width

    if h > w:                                     # portrait
        d.rectangle([0, 0, w - 1, h - 1], outline="black", width=bw)
        hdr = h // 7
        d.rectangle([bw, bw, w - 1 - bw, hdr], fill="red")
        _centre_text(d, (bw, bw, w - 1 - bw, hdr), "BLE IMAGE", "white", _font(13))

        top = hdr + h // 12
        bot = top + h // 3
        mid = w // 2
        d.rectangle([w // 12, top, mid - w // 16, bot], outline="black", width=bw)
        d.ellipse([mid + w // 16, top, w - 1 - w // 12, bot], outline="red", width=bw)

        rule = int(h * 0.78)
        d.line([(w // 8, rule), (w - w // 8, rule)], fill="black", width=bw)
        _centre_text(d, (0, int(h * 0.84), w, int(h * 0.92)),
                     "128X296", "black", _font(12))
    else:                                         # landscape
        d.rectangle([0, 0, w - 1, h - 1], outline="black", width=6)
        d.rectangle([16, 16, w - 17, 84], fill="red")
        _centre_text(d, (16, 16, w - 17, 84), "BLE IMAGE", "white", _font(40))

        top = 120
        bot = 250
        mid = w // 2
        d.rectangle([40, top, mid - 20, bot], outline="black", width=6)
        d.ellipse([mid + 20, top, w - 41, bot], outline="red", width=6)
        d.line([(40, 280), (w - 41, 280)], fill="black", width=4)
    return img


def load_image(path, w, h):
    """Fit an arbitrary image into the panel, then reduce it to the three
    inks: reddish pixels -> red, dark pixels -> black, the rest -> white."""
    src = Image.open(path).convert("RGB")
    scale = min(w / src.width, h / src.height)
    src = src.resize((max(1, int(src.width * scale)),
                      max(1, int(src.height * scale))), Image.LANCZOS)
    canvas = Image.new("RGB", (w, h), "white")
    canvas.paste(src, ((w - src.width) // 2, (h - src.height) // 2))

    # 4x4 Bayer matrix: ordered dithering, otherwise photos turn into
    # solid black blobs on a 1-bit panel.
    bayer = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]

    out = Image.new("RGB", (w, h), "white")
    sp = canvas.load()
    op = out.load()
    for y in range(h):
        for x in range(w):
            r, g, b = sp[x, y]
            lum = 0.299 * r + 0.587 * g + 0.114 * b
            if r > 110 and r - max(g, b) > 40:
                op[x, y] = (255, 0, 0)
                continue
            thresh = (bayer[y % 4][x % 4] + 0.5) * 255 / 16
            op[x, y] = (0, 0, 0) if lum < thresh else (255, 255, 255)
    return out


def to_planes(img, w, h):
    black = bytearray()
    red = bytearray()
    rowbytes = (w + 7) // 8          # 122 -> 16 bytes, 6 bits unused
    for y in range(h):
        for xb in range(rowbytes):
            b_byte = 0
            r_byte = 0
            for bit in range(8):
                x = xb * 8 + bit
                if x >= w:               # outside the glass -> white
                    b_byte |= 1 << (7 - bit)
                    r_byte |= 0
                    continue
                r, g, b = img.getpixel((x, y))
                is_black = r < 128 and g < 128 and b < 128
                is_red = r > 128 and g < 128 and b < 128
                if not is_black:
                    b_byte |= 1 << (7 - bit)      # 1 = no black
                if is_red:
                    r_byte |= 1 << (7 - bit)      # logical: 1 = red
            black.append(b_byte)
            red.append(r_byte)
    return bytes(black), bytes(red)


async def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--panel", choices=sorted(PANELS), default="hink",
                    help="panel the firmware is configured for (default: hink)")
    ap.add_argument("--name", default="EINK-N51802", help="BLE device name")
    ap.add_argument("--image", help="send this image file instead of the test card")
    args = ap.parse_args()

    w, h = PANELS[args.panel]
    if args.image:
        img = load_image(args.image, w, h)
        print(f"image: {args.image} -> {w}x{h}")
    else:
        img = make_image(w, h)
    img.save(ROOT / "build" / f"last_sent_{w}x{h}.png")
    black, red = to_planes(img, w, h)
    print(f"panel {args.panel}: {w}x{h}, "
          f"black={len(black)} red={len(red)} bytes")

    print(f"scanning for {args.name} ...")
    dev = await BleakScanner.find_device_by_name(args.name, timeout=20.0)
    if dev is None:
        print("device not found")
        return 1
    print(f"found {dev.address} ({dev.name})")

    done = asyncio.Event()

    def on_cfg(_sender, data: bytearray):
        if len(data) == 1 and data[0] == 0x01:
            done.set()

    async with BleakClient(dev, timeout=20.0) as cli:
        print(f"connected={cli.is_connected}")
        try:
            await cli.start_notify(CFG_CHR, on_cfg)   # 0x01 = refresh finished
        except Exception as e:                        # notifications are optional
            print(f"(no completion notify: {e})")
        await cli.write_gatt_char(CHR, bytes([OP_BEGIN]), response=False)
        for i in range(0, len(black), CHUNK):
            await cli.write_gatt_char(CHR, bytes([OP_DATA]) + black[i:i + CHUNK],
                                      response=False)
        print("black plane sent")
        await cli.write_gatt_char(CHR, bytes([OP_RED]), response=False)
        for i in range(0, len(red), CHUNK):
            await cli.write_gatt_char(CHR, bytes([OP_DATA]) + red[i:i + CHUNK],
                                      response=False)
        print("red plane sent")
        await cli.write_gatt_char(CHR, bytes([OP_END]), response=False)
        print("refresh requested; link stays up, waiting for the done notify")
        try:
            await asyncio.wait_for(done.wait(), timeout=90)
            print("panel finished; still connected =", cli.is_connected)
        except asyncio.TimeoutError:
            print("no completion notify; connected =", cli.is_connected)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
