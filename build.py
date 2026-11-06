#!/usr/bin/env python3
"""Build the nRF51802 e-ink application and merge it with the S110 SoftDevice.

Outputs (bin/):
  eink_n51802_app.bin     application binary (loaded at 0x18000)
  eink_n51802_app.hex     same, Intel HEX at 0x18000
  eink_n51802_full.bin    256 KB full image: S110 at 0x0 + app at 0x18000
  eink_n51802_full.hex    same, Intel HEX
"""
import os
import subprocess
import sys

WS = os.path.dirname(os.path.abspath(__file__))
TOOLCHAIN = r"C:\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin"
CC = os.path.join(TOOLCHAIN, "arm-none-eabi-gcc.exe")
OBJCOPY = os.path.join(TOOLCHAIN, "arm-none-eabi-objcopy.exe")
OBJDUMP = os.path.join(TOOLCHAIN, "arm-none-eabi-objdump.exe")
SD_HEX = os.path.join(WS, "sdk", "s110", "hex", "s110_nrf51_8.0.0_softdevice.hex")
SD_HDR = os.path.join(WS, "sdk", "s110", "headers")

OUT = os.path.join(WS, "bin")
OBJ = os.path.join(WS, "build", "obj")
APP_ORIGIN = 0x18000

SRCS = ["src/startup.c", "src/main.c", "src/eink.c"]
CFLAGS = [
    "-mcpu=cortex-m0", "-mthumb", "-O2", "-g",
    "-ffreestanding", "-nostdlib", "-fno-builtin", "-fno-stack-protector",
    "-Wall", "-Wextra",
    "-Isrc", f"-I{SD_HDR}", "-DAPP_TIMER=1",
]


def run(cmd):
    print(" ".join(cmd))
    r = subprocess.run(cmd, cwd=WS)
    if r.returncode != 0:
        sys.exit(f"command failed: {cmd[0]}")


def load_hex(path, buf):
    """Load an Intel HEX image into bytearray `buf` (0xFF-filled)."""
    base = 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(":"):
            continue
        n = int(line[1:3], 16)
        addr = int(line[3:7], 16)
        typ = int(line[7:9], 16)
        if typ == 4:
            # Extended linear address: data field follows the type nibble.
            base = int(line[9:13], 16) << 16
        elif typ == 0:
            data = bytes.fromhex(line[9:9 + 2 * n])
            a = base + addr
            buf[a:a + n] = data
        # type 1 (EOF) ignored


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OBJ, exist_ok=True)
    objs = []
    for src in SRCS:
        obj = os.path.join(OBJ, os.path.splitext(os.path.basename(src))[0] + ".o")
        run([CC, *CFLAGS, "-c", os.path.join(WS, src), "-o", obj])
        objs.append(obj)
    elf = os.path.join(OUT, "eink_n51802_app.elf")
    run([CC, "-mcpu=cortex-m0", "-mthumb", "-O2",
         "-T", os.path.join(WS, "firmware.ld"),
         "-nostdlib", "-e", "Reset_Handler",
         *objs, "-lgcc", "-o", elf])
    print(f"app elf: {elf}")

    app_bin = os.path.join(OUT, "eink_n51802_app.bin")
    run([OBJCOPY, "-O", "binary", elf, app_bin])
    app_hex = os.path.join(OUT, "eink_n51802_app.hex")
    run([OBJCOPY, "-O", "ihex", elf, app_hex])

    # merged 256 KB image
    full = bytearray(b"\xff" * (256 * 1024))
    load_hex(SD_HEX, full)
    app = open(app_bin, "rb").read()
    if APP_ORIGIN + len(app) > len(full):
        sys.exit(f"app too large: {len(app)} bytes does not fit after 0x{APP_ORIGIN:x}")
    full[APP_ORIGIN:APP_ORIGIN + len(app)] = app
    full_bin = os.path.join(OUT, "eink_n51802_full.bin")
    open(full_bin, "wb").write(full)
    print(f"full image: {full_bin} ({len(full)} bytes, app {len(app)} bytes at 0x{APP_ORIGIN:x})")

    # hex from the merged binary for flashing tools that prefer hex
    full_hex = os.path.join(OUT, "eink_n51802_full.hex")
    run([OBJCOPY, "-I", "binary", "-O", "ihex",
         "--adjust-vma=0", full_bin, full_hex])

    # size report
    r = subprocess.run([OBJDUMP, "-h", elf], cwd=WS, capture_output=True, text=True)
    print(r.stdout)


if __name__ == "__main__":
    main()
