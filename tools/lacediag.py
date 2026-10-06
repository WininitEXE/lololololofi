import sys, struct, importlib.util
spec = importlib.util.spec_from_file_location('w', sys.argv[1] + '/tools/webm2ogg.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
data = open(sys.argv[1] + '/samples/intro.webm','rb').read()

# quick scan of SimpleBlocks for the audio track with lacing details
r = m.Reader(data)
r.p = 48  # inside segment
seen = 0
f = open(sys.argv[1] + '/samples/intro.webm','rb')
# walk raw, print first audio blocks info
count = 0
pos = 0
import re
while True:
    idx = data.find(b'\xa3', pos)
    if idx < 0 or count > 12: break
    pos = idx + 1
    try:
        rr = m.Reader(data, idx + 1)
        num = m.read_vint(rr, False)
        rel = struct.unpack('>h', data[rr.p:rr.p+2])[0]; rr.p += 2
        flags = data[rr.p]
        if num == 2:
            count += 1
            size = m.read_vint(m.Reader(data, idx), False) if False else 0
            print('audio block at %d rel=%d flags=0x%02X lacing=%d count_field=%d' % (idx, rel, flags, (flags>>1)&3, (flags & 0x3F)+1))
    except Exception as e:
        pass
