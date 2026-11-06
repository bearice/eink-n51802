import re
src=open("ghidra_dump.c",encoding="utf-8",errors="replace").read()
blocks=re.split(r'(?m)^// ==== ', src)
for b in blocks:
    if not b.strip(): continue
    if '_DAT_50000724' in b or 'DAT_50000724' in b or '0x20000' in b:
        print("// ==== "+b.rstrip()[:1400]); print("="*70)
