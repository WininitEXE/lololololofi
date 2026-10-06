
import sys, re
for path in sys.argv[1:]:
    data = open(path,'rb').read(2000000)
    codes = set(re.findall(rb'A_[A-Z0-9_]+', data))
    print(path.split('\\')[-1], sorted(c.decode() for c in codes))
