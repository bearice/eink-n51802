import struct
d=open("nrf51802-flash-256k.bin","rb").read()
# Thumb: WFI=0xBF30, WFE=0xBF20, SVC=0xDFxx, CPSID=0xB672
hits={}
for o in range(0,len(d)-2,2):
    h=struct.unpack_from("<H",d,o)[0]
    if h in (0xBF30,0xBF20) or (h & 0xFF00)==0xDF00 or h in (0xB672,0xB662):
        hits.setdefault(hex(h),[]).append(o)
for k,v in hits.items():
    print(k, "count=",len(v), [hex(x) for x in v[:8]])
