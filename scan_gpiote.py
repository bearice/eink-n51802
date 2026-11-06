import struct
d=open("nrf51802-flash-256k.bin","rb").read()
for pat in (0x40006000,0x40006504,0x40000504):
    offs=[o for o in range(0,len(d)-4,4) if struct.unpack_from("<I",d,o)[0]==pat]
    print(hex(pat), [hex(o) for o in offs[:8]])
