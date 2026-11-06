"""Capture debugprobe UART with per-line timestamps; optionally reset first.

Usage: python capture_uart2.py [seconds] [baud] [port] [--reset]
Prints each received line with the wall-clock delta since the previous
line, which lets us verify delay_ms() accuracy from the TICK cadence.
"""
import subprocess
import sys
import time

import serial

args = [a for a in sys.argv[1:] if not a.startswith("--")]
flags = {a for a in sys.argv[1:] if a.startswith("--")}
secs = float(args[0]) if len(args) > 0 else 5.0
baud = int(args[1]) if len(args) > 1 else 57600
port = args[2] if len(args) > 2 else "COM16"

ser = serial.Serial(port, baud, timeout=0.05)
ser.reset_input_buffer()
t0 = time.time()
if "--reset" in flags:
    subprocess.run(
        [sys.executable, "-m", "pyocd", "reset", "-t", "nrf51", "-f", "1000000"],
        capture_output=True,
    )

line = bytearray()
last = None
total = 0
t_end = t0 + secs + (1.0 if "--reset" in flags else 0.0)
while time.time() < t_end:
    chunk = ser.read(256)
    if not chunk:
        continue
    total += len(chunk)
    for b in chunk:
        if b in (10, 13):
            if line:
                now = time.time() - t0
                dt = "" if last is None else f"  +{now - last:6.3f}s"
                print(f"[{now:6.3f}s]{dt}  {line.decode('ascii', 'replace')}")
                line.clear()
                last = now
        else:
            line.append(b)
if line:
    print(f"[tail] {line.decode('ascii', 'replace')}")
print(f"-- total {total} bytes on {port} @ {baud}")
ser.close()
