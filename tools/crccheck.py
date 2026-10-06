import importlib.util, sys
spec = importlib.util.spec_from_file_location('w', sys.argv[1] + '/tools/makeflac.py')
m = importlib.util.module_from_spec(spec); spec.loader.exec_module(m)
data = b'123456789'
print('crc8  = 0x%02X (expect 0xF4)' % m.crc8(data))
print('crc16 = 0x%04X (expect 0xFEE8)' % m.crc16(data))
