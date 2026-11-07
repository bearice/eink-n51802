"""Send configuration commands to EINK-N51802 over BLE.

Writes text commands to characteristic 0xFFF2 (the firmware's config
channel). Each argument is one command, e.g.:

    python tools/ble_cfg.py "panel=3" "save"
    python tools/ble_cfg.py "boot=1" "name=EINK-2IN9" "save"

Commands are executed in order; `save` persists them to flash.
"""
import asyncio
import sys

from bleak import BleakClient, BleakScanner

CHR = "0000fff2-0000-1000-8000-00805f9b34fb"


async def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    cmds = sys.argv[1:]

    dev = await BleakScanner.find_device_by_name("EINK-N51802", timeout=20.0)
    if dev is None:
        print("device not found")
        return 1
    print(f"found {dev.address}")

    async with BleakClient(dev, timeout=20.0) as cli:
        print(f"connected={cli.is_connected}")
        for c in cmds:
            payload = c.encode("ascii")
            if len(payload) > 20:
                print(f"skip (too long): {c}")
                continue
            await cli.write_gatt_char(CHR, payload, response=False)
            print(f"-> {c}")
            await asyncio.sleep(0.15)
        await asyncio.sleep(0.3)
    print("done")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
