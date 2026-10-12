import math
import struct
import sys

source, out = sys.argv[1], sys.argv[2]
b = open(source, 'rb').read()
if b[0] != 0x99:
    sys.exit('not an array with a two-byte count')
count = int.from_bytes(b[1:3], 'big')
widths = {0xf9: (2, '>e'), 0xfa: (4, '>f'), 0xfb: (8, '>d')}
values = []
pos = 3
for _ in range(count):
    size, fmt = widths[b[pos]]
    x = struct.unpack(fmt, b[pos + 1:pos + 1 + size])[0]
    if math.isnan(x):
        sys.exit('a NaN is not widened by this script')
    values.append((b[pos], x))
    pos += 1 + size
if pos != len(b):
    sys.exit('bytes after the array')
head_cbor = b[:3]
head_mp = b'\xdc' + count.to_bytes(2, 'big')
open(out + '/floats64.cbor', 'wb').write(head_cbor + b''.join(b'\xfb' + struct.pack('>d', x) for _, x in values))
open(out + '/floats64.msgpack', 'wb').write(head_mp + b''.join(b'\xcb' + struct.pack('>d', x) for _, x in values))
open(out + '/floats.msgpack', 'wb').write(head_mp + b''.join(
    b'\xcb' + struct.pack('>d', x) if w == 0xfb else b'\xca' + struct.pack('>f', x) for w, x in values))
print(count, {hex(w): sum(1 for v, _ in values if v == w) for w in widths})
