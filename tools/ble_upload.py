"""Upload a test image to EINK-N51802 over BLE and trigger a refresh.

Renders a 400x300 3-colour image, converts it into the panel's two bit
planes, and streams them to characteristic FFF1 using the firmware's
packet protocol:
    0x01 | begin (panel reset+init, black plane open)
    0x02 | <up to 19 image bytes>
    0x03 | switch to the red plane
    0x04 | end -> refresh
Plane encoding: black plane bit 0 = black; red plane bit 0 = red
(the controller takes the red plane inverted, so 1 = no red).
"""
import asyncio
import sys
from pathlib import Path

from bleak import BleakClient, BleakScanner
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parent.parent
W, H = 400, 300
CHR = "0000fff1-0000-1000-8000-00805f9b34fb"
CHUNK = 19
OP_BEGIN, OP_DATA, OP_RED, OP_END, OP_RECT = 0x01, 0x02, 0x03, 0x04, 0x05


def make_image():
    img = Image.new("RGB", (W, H), "white")
    d = ImageDraw.Draw(img)
    d.rectangle([0, 0, W - 1, H - 1], outline="black", width=6)
    d.rectangle([16, 16, W - 17, 84], fill="red")
    try:
        font = ImageFont.load_default(size=54)
    except TypeError:
        font = ImageFont.load_default()
    d.text((30, 22), "BLE IMAGE", fill="white", font=font)
    d.rectangle([40, 120, 150, 260], outline="black", width=6)
    d.ellipse([200, 120, 330, 250], outline="red", width=6)
    d.line([(40, 280), (360, 280)], fill="black", width=4)
    img.save(ROOT / "docs" / "ble_upload_example.png")
    return img


def to_planes(img):
    black = bytearray()
    red = bytearray()
    for y in range(H):
        for xb in range(W // 8):
            b_byte = 0
            r_byte = 0
            for bit in range(8):
                x = xb * 8 + bit
                r, g, b = img.getpixel((x, y))
                is_black = r < 128 and g < 128 and b < 128
                is_red = r > 128 and g < 128 and b < 128
                if not is_black:
                    b_byte |= 1 << (7 - bit)
                if not is_red:
                    r_byte |= 1 << (7 - bit)
            black.append(b_byte)
            red.append(r_byte)
    return bytes(black), bytes(red)


def to_planes_rect(img, x, y, w, h):
    """Bytes for just the given rectangle (used by the partial update)."""
    black = bytearray()
    red = bytearray()
    stride = (w + 7) // 8
    for row in range(h):
        for xb in range(stride):
            b_byte = 0
            r_byte = 0
            for bit in range(8):
                px = x + xb * 8 + bit
                if px >= x + w:
                    b_byte |= 1 << (7 - bit)      # outside -> white
                    r_byte |= 1 << (7 - bit)
                    continue
                r, g, b = img.getpixel((px, y + row))
                is_black = r < 128 and g < 128 and b < 128
                is_red = r > 128 and g < 128 and b < 128
                if not is_black:
                    b_byte |= 1 << (7 - bit)
                if not is_red:
                    r_byte |= 1 << (7 - bit)
            black.append(b_byte)
            red.append(r_byte)
    return bytes(black), bytes(red)

async def main():
    img = make_image()
    black, red = to_planes(img)
    print(f"planes: black={len(black)} red={len(red)} bytes")

    print("scanning for EINK-N51802 ...")
    dev = await BleakScanner.find_device_by_name("EINK-N51802", timeout=20.0)
    if dev is None:
        print("device not found")
        return 1
    print(f"found {dev.address} ({dev.name})")

    rect = None
    if len(sys.argv) > 1 and sys.argv[1] == "--rect":
        rect = tuple(int(v) for v in sys.argv[2:6])   # x y w h

    async with BleakClient(dev, timeout=20.0) as cli:
        print(f"connected={cli.is_connected}")
        if rect:
            x, y, w, h = rect
            pb, pr = to_planes_rect(img, x, y, w, h)
            print(f"partial {w}x{h} at ({x},{y}) -> {len(pb)} bytes/plane")
            cmd = bytes([OP_RECT]) + bytes([x & 0xFF, x >> 8, y & 0xFF, y >> 8,
                                            w & 0xFF, w >> 8, h & 0xFF, h >> 8])
            await cli.write_gatt_char(CHR, cmd, response=False)
            for i in range(0, len(pb), CHUNK):
                await cli.write_gatt_char(CHR, bytes([OP_DATA]) + pb[i:i + CHUNK],
                                          response=False)
            for i in range(0, len(pr), CHUNK):
                await cli.write_gatt_char(CHR, bytes([OP_DATA]) + pr[i:i + CHUNK],
                                          response=False)
            await cli.write_gatt_char(CHR, bytes([OP_END]), response=False)
            print("partial refresh requested; waiting")
            await asyncio.sleep(25)
            return 0
        await cli.write_gatt_char(CHR, bytes([0x01]), response=False)
        sent = 0
        for i in range(0, len(black), CHUNK):
            await cli.write_gatt_char(CHR, bytes([0x02]) + black[i:i + CHUNK],
                                      response=False)
            sent += 1
            if sent % 100 == 0:
                await asyncio.sleep(0)      # let the stack breathe
        print("black plane sent")
        for i in range(0, len(red), CHUNK):
            await cli.write_gatt_char(CHR, bytes([0x02]) + red[i:i + CHUNK],
                                      response=False)
        print("red plane sent")
        await cli.write_gatt_char(CHR, bytes([0x04]), response=False)
        print("refresh requested; waiting for the panel")
        await asyncio.sleep(25)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
