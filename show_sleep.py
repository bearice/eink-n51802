import re
src=open("ghidra_dump.c",encoding="utf-8",errors="replace").read()
blocks=re.split(r'(?m)^// ==== ', src)
targets={0x2338,0x4172,0x420c,0x42c0}
for b in blocks:
    if not b.strip(): continue
    head=b.split("\n",1)[0]
    m=re.search(r'@\s*([0-9a-f]+)',head)
    if not m: continue
    start=int(m.group(1),16)
    size=int(re.search(r'size=(\d+)',head).group(1))
    if any(start<=t<start+size for t in targets):
        print("// ==== "+b.rstrip()[:1500]); print("="*70)
