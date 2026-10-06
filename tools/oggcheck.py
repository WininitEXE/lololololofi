import sys
d = open(sys.argv[1],'rb').read()
print('size', len(d), 'magic', d[:4])
pos = 0
for page in range(4):
    if d[pos:pos+4] != b'OggS':
        print('no OggS at', pos); break
    seg = d[pos+26]; table = d[pos+27:pos+27+seg]
    bodylen = sum(table)
    hdrtype = d[pos+5]
    gran = int.from_bytes(d[pos+6:pos+14],'little',signed=True)
    serial = int.from_bytes(d[pos+14:pos+18],'little')
    seq = int.from_bytes(d[pos+18:pos+22],'little')
    body = d[pos+27+seg:pos+27+seg+bodylen]
    print('page%d pos=%d hdrtype=0x%02X gran=%d serial=0x%X seq=%d segs=%s body0=%s' % (
        page, pos, hdrtype, gran, serial, seq, list(table), body[:12].hex()))
    pos += 27 + seg + bodylen
