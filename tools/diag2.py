import sys, struct, importlib.util
spec = importlib.util.spec_from_file_location('w', sys.argv[1] + '/tools/webm2ogg.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
data = open(sys.argv[1] + '/samples/intro.webm','rb').read()
r = m.Reader(data)
CAP = 30000
count = [0]
trace = []
stall = [0]

def walk(rr, end, depth):
    while rr.p < end:
        count[0] += 1
        if count[0] > CAP:
            stall[0] = 1
            return
        start = rr.p
        eid = m.read_vint(rr, True)
        size = m.read_vint(rr, False)
        be = min(rr.p + size, end)
        if count[0] < 40 or count[0] % 1000 == 0:
            trace.append('  ' * depth + 'n=%d pos=%d id=0x%X size=%d' % (count[0], start, eid, size))
        if eid in m.MASTER:
            walk(rr, be, depth + 1)
            rr.p = be
            continue
        rr.p = be

walk(r, len(data), 0)
print('\n'.join(trace[:60]))
print('...')
print('\n'.join(trace[-15:]))
print('count=%d stalled=%d pos=%d/%d' % (count[0], stall[0], r.p, len(data)))
