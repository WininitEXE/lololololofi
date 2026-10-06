import sys, struct
d = open(sys.argv[1],'rb').read()
assert d[:4] == b'fLaC', d[:4]
pos = 4
while True:
    hdr = d[pos]
    last = hdr >> 7; typ = hdr & 0x7F
    size = int.from_bytes(d[pos+1:pos+4],'big')
    print('metadata block type=%d last=%d size=%d' % (typ, last, size))
    if typ == 0:
        si = d[pos+4:pos+4+34]
        mn, mx = struct.unpack_from('>HH', si, 0)
        mnf = int.from_bytes(si[4:7],'big'); mxf = int.from_bytes(si[7:10],'big')
        bits = int.from_bytes(si[10:18],'big')
        rate = bits >> 44; ch = ((bits >> 41) & 7) + 1; bps = ((bits >> 36) & 31) + 1
        total = bits & ((1<<36)-1)
        print('  blocksize %d..%d framesize %d..%d rate=%d ch=%d bps=%d total=%d' % (mn,mx,mnf,mxf,rate,ch,bps,total))
    pos += 4 + size
    if last: break
print('first frame at', pos, 'bytes:', d[pos:pos+16].hex())
def crc8(data):
    c = 0
    for b in data:
        c ^= b
        for _ in range(8): c = ((c<<1)^0x07)&0xFF if c&0x80 else (c<<1)&0xFF
    return c
def crc16(data):
    c = 0
    for b in data:
        c ^= b<<8
        for _ in range(8): c = ((c<<1)^0x8005)&0xFFFF if c&0x8000 else (c<<1)&0xFFFF
    return c
# verify first frame header crc
hdr = d[pos:pos+8]
print('header crc8 stored=%02X computed=%02X' % (hdr[7], crc8(hdr[:7])))
print('sync bits', bin(hdr[0]), bin(hdr[1]))
