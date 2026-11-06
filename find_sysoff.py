import struct
d=open("nrf51802-flash-256k.bin","rb").read()
def h(o): return struct.unpack_from("<H",d,o)[0]
# find LDR (literal) thumb instructions: 01001 Rt imm8  (0x4800..0x4FFF)
for o in range(0,len(d)-2,2):
    ins=h(o)
    if (ins & 0xF800) != 0x4800: continue
    rt=(ins>>8)&7; imm=(ins&0xFF)*4
    pc=(o+4)&~3
    la=pc+imm
    if la+4>len(d): continue
    val=struct.unpack_from("<I",d,la)[0]
    if val!=0x40000500: continue
    print(f"code 0x{o:05X}: ldr r{rt},=0x40000500 (pool@0x{la:05X})")
    for k in range(2,14,2):
        i2=h(o+k)
        print(f"    +{k}: 0x{i2:04X}")
