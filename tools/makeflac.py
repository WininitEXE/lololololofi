"""Minimal FLAC encoder (verbatim subframes) - only used to create test material."""
import struct, sys, math

class BitWriter:
    def __init__(self): self.bits = []
    def write(self, value, n):
        for i in range(n - 1, -1, -1): self.bits.append((value >> i) & 1)
    def write_signed(self, value, n):
        self.write(value & ((1 << n) - 1), n)
    def align(self):
        while len(self.bits) % 8: self.bits.append(0)
    def bytes(self):
        out = bytearray()
        for i in range(0, len(self.bits), 8):
            b = 0
            for k in range(8): b = (b << 1) | self.bits[i + k]
            out.append(b)
        return bytes(out)

def crc8(data):
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if crc & 0x80 else (crc << 1) & 0xFF
    return crc

def crc16(data):
    crc = 0
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x8005) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc

def utf8_number(n):
    if n < 0x80: return bytes([n])
    if n < 0x800: return bytes([0xC0 | (n >> 6), 0x80 | (n & 0x3F)])
    if n < 0x10000: return bytes([0xE0 | (n >> 12), 0x80 | ((n >> 6) & 0x3F), 0x80 | (n & 0x3F)])
    return bytes([0xF0 | (n >> 18), 0x80 | ((n >> 12) & 0x3F), 0x80 | ((n >> 6) & 0x3F), 0x80 | (n & 0x3F)])

def encode(samples, channels, rate, bits=16, blocksize=4096):
    """samples: list of per-channel lists of ints"""
    total = len(samples[0])
    out = bytearray(b'fLaC')
    # STREAMINFO
    si = BitWriter()
    si.write(blocksize, 16); si.write(blocksize, 16)
    si.write(0, 24); si.write(0, 24)             # min/max frame size (unknown)
    si.write(rate, 20); si.write(channels - 1, 3); si.write(bits - 1, 5); si.write(total, 36)
    si.align()
    si_bytes = si.bytes() + bytes(16)            # md5 = zeros
    out += bytes([0x80]) + struct.pack('>I', 34)[1:] + si_bytes   # last block, type 0, len 34

    frame_no = 0
    pos = 0
    max_frame = 0
    min_frame = 1 << 24
    while pos < total:
        n = min(blocksize, total - pos)
        bw = BitWriter()
        bw.write(0b11111111111110, 14)   # sync
        bw.write(0, 1)                   # reserved
        bw.write(0, 1)                   # fixed block size
        bw.write(0b0111, 4)              # 16 bit block size - 1 at end of header
        bw.write(0b0000, 4)              # sample rate from STREAMINFO
        bw.write(0b0001 if channels == 2 else 0b0000, 4)  # channel assignment
        bw.write(0b100, 3)               # 16 bits per sample
        bw.write(0, 1)                   # reserved
        bw.write(frame_no, 8)            # frame number (small values only)
        bw.write(n - 1, 16)              # block size - 1
        header = bw.bytes()
        frame = bytearray(header)
        frame.append(crc8(header))
        body = BitWriter()
        for c in range(channels):
            body.write(0, 1)             # no wasted bits
            body.write(0b000001, 6)      # verbatim
            for i in range(pos, pos + n):
                body.write_signed(samples[c][i], bits)
        body.align()
        frame += body.bytes()
        frame += struct.pack('>H', crc16(bytes(frame)))
        max_frame = max(max_frame, len(frame))
        min_frame = min(min_frame, len(frame))
        out += frame
        pos += n
        frame_no += 1
    # patch frame size fields: STREAMINFO starts at offset 8, min/max framesize at +4 and +7
    if min_frame > max_frame: min_frame = max_frame
    out[12:15] = struct.pack('>I', min_frame)[1:]
    out[15:18] = struct.pack('>I', max_frame)[1:]
    return bytes(out)

def main():
    import wave
    src, dst = sys.argv[1], sys.argv[2]
    w = wave.open(src, 'rb')
    ch, sw, rate, nf = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    raw = w.readframes(nf)
    w.close()
    assert sw == 2, 'expected 16 bit source'
    chans = [[] for _ in range(ch)]
    for i in range(nf):
        for c in range(ch):
            v = struct.unpack_from('<h', raw, (i*ch + c)*2)[0]
            chans[c].append(v)
    data = encode(chans, ch, rate)
    open(dst, 'wb').write(data)
    print('wrote %s: %d bytes, %d frames, %d ch, %d Hz' % (dst, len(data), nf, ch, rate))

if __name__ == '__main__':
    main()
