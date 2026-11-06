import re
src=open("ghidra_dump.c",encoding="utf-8",errors="replace").read()
blocks=re.split(r'(?m)^// ==== ', src)
for b in blocks:
    if not b.strip(): continue
    head=b.split("\n",1)[0]
    if re.search(r'void main|FUN_000003c2|FUN_00000164|DAT_00000a18', b):
        print("// ==== "+b.rstrip()[:1200]); print("="*70)
