"""Create a small stereo test WAV (sweep + tone) so we have known content."""
import wave, struct, math, sys
path = sys.argv[1]
rate = 44100; secs = 4.0
w = wave.open(path, 'wb'); w.setnchannels(2); w.setsampwidth(2); w.setframerate(rate)
frames = bytearray()
n = int(rate*secs)
for i in range(n):
    t = i/rate
    f = 200 + 1200*(i/n)
    v = 0.6*math.sin(2*math.pi*f*t) + 0.25*math.sin(2*math.pi*440*t)
    l = int(max(-1,min(1,v))*32000)
    r = int(max(-1,min(1,v*0.8))*32000)
    frames += struct.pack('<hh', l, r)
w.writeframes(bytes(frames)); w.close()
print('wrote', path)
