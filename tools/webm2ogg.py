"""Extract the Vorbis/Opus audio track of a WebM (Matroska) file into a real .ogg file.

Used only to create test material for the 1-bit converter, because this machine has
no .ogg files but plenty of .webm ones.
"""
import struct, sys, zlib

class Reader:
    def __init__(self, data, pos=0, end=None):
        self.d = data; self.p = pos; self.end = len(data) if end is None else end
    def eof(self): return self.p >= self.end

def read_vint(r, keep_marker):
    b = r.d[r.p]; r.p += 1
    length = 1
    mask = 0x80
    while not (b & mask):
        mask >>= 1; length += 1
        if length > 8: raise ValueError('bad vint')
    val = b if keep_marker else (b & (mask - 1))
    for _ in range(length - 1):
        val = (val << 8) | r.d[r.p]; r.p += 1
    return val

def read_uint(r, size): 
    v = 0
    for _ in range(size): v = (v << 8) | r.d[r.p]; r.p += 1
    return v

def read_float(r, size):
    if size == 4: return struct.unpack('>f', r.d[r.p:r.p+4])[0]
    if size == 8: return struct.unpack('>d', r.d[r.p:r.p+8])[0]
    raise ValueError('bad float size')

MASTER = {0x18538067,0x1654AE6B,0xAE,0xE1,0x1F43B675,0xA0,0xE0,0xBB,0x1254C367}
_COUNT = 0

def parse_track_entry(r, end):
    t = {}
    while r.p < end:
        eid = read_vint(r, True)
        size = read_vint(r, False)
        be = min(r.p + size, end)
        if eid == 0xD7:
            t['num'] = read_uint(r, size)
        elif eid == 0x83:
            t['type'] = read_uint(r, size)
        elif eid == 0x86:
            t['codec'] = r.d[r.p:be].decode('ascii', 'replace')
        elif eid == 0x63A2:
            t['priv'] = r.d[r.p:be]
        elif eid == 0xE1:
            while r.p < be:
                ieid = read_vint(r, True); isize = read_vint(r, False)
                ibe = min(r.p + isize, be)
                if ieid == 0xB5: t['rate'] = read_float(r, isize)
                elif ieid == 0x9F: t['channels'] = read_uint(r, isize)
                r.p = ibe
        r.p = be
    return t

def parse(data):
    root = Reader(data)
    info = {'tracks': {}, 'blocks': [], 'ts_scale': 1000000}
    def walk(r, end, path):
        global _COUNT
        while r.p < end:
            _COUNT += 1
            if _COUNT % 200 == 0:
                print('  ...elements', _COUNT, 'pos', r.p, 'end', end, flush=True)
            eid = read_vint(r, True)
            size = read_vint(r, False)
            body_end = min(r.p + size, end)
            if eid == 0xAE:
                t = parse_track_entry(r, body_end)
                info['tracks'][t.get('num', -1)] = t
                r.p = body_end
                continue
            if eid in MASTER:
                walk(r, body_end, path + [eid])
                r.p = body_end
                continue
            if eid == 0x2AD7B1:  # TimestampScale
                info['ts_scale'] = read_uint(r, size)
            elif eid == 0xA3 or eid == 0xA1:  # SimpleBlock / Block
                num = read_vint(r, False)
                rel = struct.unpack('>h', r.d[r.p:r.p+2])[0]; r.p += 2
                flags = r.d[r.p]; r.p += 1
                frames = []
                if eid == 0xA3:
                    lacing = (flags >> 1) & 3
                    if lacing == 0:
                        frames = [r.d[r.p:body_end]]
                    elif lacing == 1:
                        sizes = []; i = 0
                        while True:
                            v = 0
                            while True:
                                b = r.d[r.p]; r.p += 1; v += b
                                if b != 255: break
                            sizes.append(v)
                            if r.p >= body_end: break
                        total = sum(sizes); rest = body_end - r.p - total
                        sizes.append(rest)
                        for s in sizes:
                            frames.append(r.d[r.p:r.p+s]); r.p += s
                    elif lacing == 2:
                        n = r.d[r.p]; r.p += 1
                        cnt = n + 1; total = body_end - r.p
                        each = total // cnt
                        for i in range(cnt):
                            frames.append(r.d[r.p:r.p+each]); r.p += each
                    else:
                        n = r.d[r.p]; r.p += 1
                        cnt = n + 1; sizes = []
                        first = read_vint(r, False); sizes.append(first)
                        for _ in range(cnt - 2):
                            sizes.append(read_vint(r, False) + first)
                        sizes.append(body_end - r.p - sum(sizes))
                        for s in sizes:
                            frames.append(r.d[r.p:r.p+s]); r.p += s
                    info['blocks'].append((num, rel, frames))
                    info.setdefault('blockinfo', []).append((num, (flags >> 1) & 3, len(frames), [len(f) for f in frames][:6]))
                else:
                    info['blocks'].append((num, rel, [r.d[r.p:body_end]]))
                    info.setdefault('blockinfo', []).append((num, -2, 1, [body_end - r.p]))
                r.p = body_end
                continue
            else:
                pass
            r.p = body_end
    walk(root, len(data), [])
    return info

def xiph_lacing_split(priv):
    """Matroska Vorbis CodecPrivate: [count-1][lace sizes][packets]"""
    pos = 0
    count = priv[pos] + 1; pos += 1
    sizes = []
    for _ in range(count - 1):
        v = 0
        while True:
            b = priv[pos]; pos += 1; v += b
            if b != 255: break
        sizes.append(v)
    sizes.append(len(priv) - pos - sum(sizes))
    packets = []
    for s in sizes:
        packets.append(priv[pos:pos+s]); pos += s
    return packets

_CRC_TABLE = []
for _i in range(256):
    _c = _i << 24
    for _ in range(8):
        _c = ((_c << 1) ^ 0x04c11db7) & 0xFFFFFFFF if _c & 0x80000000 else (_c << 1) & 0xFFFFFFFF
    _CRC_TABLE.append(_c)

def ogg_crc(data):
    crc = 0
    for b in data:
        crc = ((crc << 8) & 0xFFFFFFFF) ^ _CRC_TABLE[((crc >> 24) & 0xFF) ^ b]
    return crc

def make_page(serial, seq, granule, packets, header_type):
    segs = []
    body = b''
    for pkt in packets:
        n = len(pkt)
        while n >= 255:
            segs.append(255); n -= 255
        segs.append(n)
        body += pkt
    assert len(segs) <= 255, 'page too large'
    hdr = b'OggS' + bytes([0, header_type]) + struct.pack('<q', granule if granule is not None else -1)
    hdr += struct.pack('<II', serial, seq) + b'\x00\x00\x00\x00' + bytes([len(segs)]) + bytes(segs)
    page = hdr + body
    crc = ogg_crc(page)
    return page[:22] + struct.pack('<I', crc) + page[26:]

def main():
    src, dst = sys.argv[1], sys.argv[2]
    data = open(src, 'rb').read()
    info = parse(data)
    audio = None
    for num, t in info['tracks'].items():
        if t.get('type') == 2:
            audio = (num, t)
    if audio is None:
        print('no audio track'); return 1
    num, t = audio
    codec = t.get('codec', '')
    rate = int(t.get('rate', 48000)); channels = int(t.get('channels', 2))
    print('audio track %d codec=%s rate=%d ch=%d' % (num, codec, rate, channels))

    if 'VORBIS' in codec:
        headers = xiph_lacing_split(t['priv'])
        print('vorbis headers:', [len(h) for h in headers])
    elif 'OPUS' in codec:
        head = t['priv']
        vendor = b'webm2ogg'
        tags = b'OpusTags' + struct.pack('<I', len(vendor)) + vendor + struct.pack('<I', 0)
        headers = [head, tags]
    else:
        print('unsupported codec'); return 1

    ts_scale = info['ts_scale']
    blocks = [b for b in info['blocks'] if b[0] == num]
    print('audio blocks:', len(blocks))
    serial = 0x31415926
    out = bytearray([0] * 0)
    out += make_page(serial, 0, 0, [headers[0]], 0x02)
    out += make_page(serial, 1, 0, headers[1:], 0x00)
    seq = 2
    packets = []
    granules = []
    for (n, rel, frames) in blocks:
        ts_ms = rel * ts_scale / 1e6
        g = int(ts_ms * rate / 1000.0)
        for f in frames:
            packets.append(f); granules.append(g)
    # one packet per page keeps it simple and still valid
    for pkt, g in zip(packets, granules):
        out += make_page(serial, seq, g, [pkt], 0x00)
        seq += 1
    # mark last page EOS
    open(dst, 'wb').write(bytes(out))
    print('wrote %s (%d bytes, %d pages)' % (dst, len(out), seq))
    return 0

if __name__ == '__main__':
    sys.exit(main())
