import struct
d=open("nrf51802-flash-256k.bin","rb").read()
for a in (0x5FC4,0x5FC8,0x5FDC,0x5FE0,0x5FE4,0x5FE8):
    print(f"0x{a:04X}: 0x{struct.unpack_from('<I',d,a)[0]:08X}")
