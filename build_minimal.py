#!/usr/bin/env python3
"""Build the standalone minimal self-test (no SoftDevice, at 0x0).

Outputs (bin/):
  minimal.elf / minimal.bin / minimal.hex
"""
import os
import subprocess
import sys

WS = os.path.dirname(os.path.abspath(__file__))
TOOLCHAIN = r"C:\xpack-arm-none-eabi-gcc-15.2.1-1.1\bin"
CC = os.path.join(TOOLCHAIN, "arm-none-eabi-gcc.exe")
OBJCOPY = os.path.join(TOOLCHAIN, "arm-none-eabi-objcopy.exe")
OBJDUMP = os.path.join(TOOLCHAIN, "arm-none-eabi-objdump.exe")

OUT = os.path.join(WS, "bin")
OBJ = os.path.join(WS, "build", "obj-min")
SRCS = ["src/startup.c", "src/minimal.c"]
CFLAGS = [
    "-mcpu=cortex-m0", "-mthumb", "-O2", "-g",
    "-ffreestanding", "-nostdlib", "-fno-builtin", "-fno-stack-protector",
    "-Wall", "-Wextra",
    "-Isrc",
]


def run(cmd):
    print(" ".join(cmd))
    r = subprocess.run(cmd, cwd=WS)
    if r.returncode != 0:
        sys.exit(f"command failed: {cmd[0]}")


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OBJ, exist_ok=True)
    objs = []
    for src in SRCS:
        obj = os.path.join(OBJ, os.path.splitext(os.path.basename(src))[0] + ".o")
        run([CC, *CFLAGS, "-c", os.path.join(WS, src), "-o", obj])
        objs.append(obj)
    elf = os.path.join(OUT, "minimal.elf")
    run([CC, "-mcpu=cortex-m0", "-mthumb", "-O2",
         "-T", os.path.join(WS, "minimal.ld"),
         "-nostdlib", "-e", "Reset_Handler",
         *objs, "-lgcc", "-o", elf])
    print(f"minimal elf: {elf}")

    run([OBJCOPY, "-O", "binary", elf, os.path.join(OUT, "minimal.bin")])
    run([OBJCOPY, "-O", "ihex", elf, os.path.join(OUT, "minimal.hex")])
    r = subprocess.run([OBJDUMP, "-h", elf], cwd=WS, capture_output=True, text=True)
    print(r.stdout)
    # report first two words (SP, Reset) for vector-table sanity
    with open(os.path.join(OUT, "minimal.bin"), "rb") as f:
        head = f.read(8)
    import struct
    sp, rst = struct.unpack("<II", head)
    print(f"vector: SP=0x{sp:08X} Reset=0x{rst:08X} size={os.path.getsize(os.path.join(OUT, 'minimal.bin'))}B")


if __name__ == "__main__":
    main()
