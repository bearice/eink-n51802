import struct
d=open("nrf51802-flash-256k.bin","rb").read()
for pat in (0x40000500,0x40000400,0x40000000):
    offs=[o for o in range(0,len(d)-4,4) if struct.unpack_from("<I",d,o)[0]==pat]
    print(hex(pat), [hex(o) for o in offs[:6]])
