import sys, struct, importlib.util
spec = importlib.util.spec_from_file_location('w', sys.argv[1] + '/tools/webm2ogg.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
data = open(sys.argv[1] + '/samples/intro.webm','rb').read()
r = m.Reader(data)
cap = 0
log = []
depth = 0
while r.p < len(data) and cap < 4000:
    cap += 1
    start = r.p
    eid = m.read_vint(r, True)
    size = m.read_vint(r, False)
    if cap < 60 or (cap % 500 == 0):
        log.append('cap=%d pos=%d id=0x%X size=%d' % (cap, start, eid, size))
    be = min(r.p + size, len(data))
    if size > 10_000_000:
        log.append('BIG size=%d at pos=%d id=0x%X' % (size, start, eid))
    r.p = be
print('\n'.join(log[-40:]))
print('stopped at cap', cap, 'pos', r.p, 'of', len(data))
