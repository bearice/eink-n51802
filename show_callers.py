import re
src=open("ghidra_dump.c",encoding="utf-8",errors="replace").read()
blocks=re.split(r'(?m)^// ==== ', src)
for b in blocks:
    if not b.strip(): continue
    name=b.split()[0]
    if 'FUN_0000082c(' in b and name!='FUN_0000082c':
        print("CALLER:", name)
        print(b.rstrip()[:900]); print('-'*60)
