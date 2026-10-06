"""Generates res/app.ico - a 1bit square wave icon (pure python, no deps)."""
import struct, zlib, math, os

def png_bytes(w, h, pixels):
    raw = b''
    for y in range(h):
        raw += b'\x00' + bytes(pixels[y*w*4:(y+1)*w*4])
    def chunk(tag, data):
        c = tag + data
        return struct.pack('>I', len(data)) + c + struct.pack('>I', zlib.crc32(c) & 0xFFFFFFFF)
    ihdr = struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', ihdr) +
            chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))

def render(size):
    px = bytearray(size*size*4)
    cx = size / 2.0
    for y in range(size):
        for x in range(size):
            u = (x + 0.5) / size
            v = (y + 0.5) / size
            # background: dark navy rounded square
            edge = 1.0 / size
            inset = 0.04
            inside = inset <= u <= 1-inset and inset <= v <= 1-inset
            r, g, b, a = 0, 0, 0, 0
            if inside:
                t = v
                r = int(18 + 26*t); g = int(22 + 30*t); b = int(34 + 44*t); a = 255
                # square wave: 1 bit -> only two levels
                period = 0.25
                phase = (u - 0.12) / period
                level = 1 if (int(phase) % 2 == 0) else 0
                wy = 0.28 if level else 0.72
                thickness = max(1.0/size, 0.075)
                if abs(v - wy) < thickness/2:
                    r, g, b = 255, 72, 72
                # vertical connectors to make it look like a real square wave
                if level == 1 and 0.28 <= v <= 0.72:
                    k = (u - 0.12) / period
                    if abs(k - round(k)) < (0.5/size) and int(round(k)) % 2 == 0:
                        r, g, b = 255, 72, 72
            px[(y*size + x)*4:(y*size + x)*4+4] = bytes((r, g, b, a))
    return png_bytes(size, size, px)

sizes = [16, 24, 32, 48, 64, 128, 256]
images = [(s, render(s)) for s in sizes]
out = bytearray()
out += struct.pack('<HHH', 0, 1, len(images))
offset = 6 + 16*len(images)
for s, data in images:
    w = 0 if s == 256 else s
    out += struct.pack('<BBBBHHII', w, w, 0, 0, 1, 32, len(data), offset)
    offset += len(data)
for s, data in images:
    out += data
path = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'res', 'app.ico')
os.makedirs(os.path.dirname(path), exist_ok=True)
open(path, 'wb').write(bytes(out))
print('wrote', path, len(out), 'bytes')
