import sys, importlib.util
spec = importlib.util.spec_from_file_location('w', sys.argv[1] + '/tools/webm2ogg.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
data = open(sys.argv[1] + '/samples/intro.webm','rb').read()
info = m.parse(data)
bi = info.get('blockinfo', [])
aud = [b for b in bi if b[0] == 2]
print('total blocks', len(bi), 'audio blocks', len(aud))
from collections import Counter
print('lacing histogram (audio):', Counter(b[1] for b in aud))
print('first 10 audio blockinfos:', aud[:10])
print('first 5 video blockinfos:', [b for b in bi if b[0] == 1][:5])
tot = sum(b[2] for b in aud)
print('audio frames total', tot)
