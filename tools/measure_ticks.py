import time, serial
ser = serial.Serial("COM16", 57600, timeout=0.05)
ser.reset_input_buffer()
buf = bytearray(); t0 = time.time(); stamps = []
while time.time() - t0 < 8.0:
    c = ser.read(512)
    if not c: continue
    for b in c:
        buf.append(b)
        if b == 10:
            stamps.append((time.time() - t0, bytes(buf).decode("ascii","replace").strip()))
            buf.clear()
ser.close()
ts = [s for s in stamps if s[1].startswith("TICK")]
print(f"tick_lines={len(ts)}")
prev = None
for t, line in ts:
    d = "" if prev is None else f"  interval={t-prev:.4f}s"
    print(f"{t:7.3f}s {line}{d}")
    prev = t
if len(ts) > 1:
    print(f"mean interval = {(ts[-1][0]-ts[0][0])/(len(ts)-1):.4f}s  (expect 4 x 250ms + print time)")
