import wave, struct, math, sys
rate = 22050; secs = 62.0
w = wave.open(sys.argv[1],'wb'); w.setnchannels(1); w.setsampwidth(2); w.setframerate(rate)
n = int(rate*secs); out = bytearray()
for i in range(n):
    t = i/rate
    env = 0.5*(1+math.sin(2*math.pi*0.4*t))
    v = env*math.sin(2*math.pi*(300+200*math.sin(2*math.pi*0.7*t))*t)
    out += struct.pack('<h', int(max(-1,min(1,v))*30000))
w.writeframes(bytes(out)); w.close()
print('wrote', sys.argv[1], n, 'frames')
