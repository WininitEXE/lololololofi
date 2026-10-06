import sys, wave, struct, collections
for path in sys.argv[1:]:
    try:
        w = wave.open(path, 'rb')
    except Exception as e:
        print(path, 'open failed:', e); continue
    n, sw, fr, nf = w.getnchannels(), w.getsampwidth(), w.getframerate(), w.getnframes()
    data = w.readframes(min(nf, 200000))
    w.close()
    counter = collections.Counter(data)
    vals = sorted(counter)[:6]
    print('%-34s ch=%d bytes=%d rate=%d frames=%d distinct=%d sample_values=%s' % (
        path.split('\\')[-1], n, sw, fr, nf, len(counter), vals))
