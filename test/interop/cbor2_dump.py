# cbor2: the canonical dump of one top-level item, the same text as dump.hpp. cbor2 gives a tag that it does
# not know as CBORTag (RFC 8949 3.4), resolves tags 28 and 29, and turns tags 2 and 3 into an int.
import math
import struct
import sys

import cbor2


def dump(v):
    if v is True:
        return "s(21)"
    if v is False:
        return "s(20)"
    if v is None:
        return "s(22)"
    if v is cbor2.undefined:
        return "s(23)"
    if isinstance(v, cbor2.CBORSimpleValue):
        return "s(%d)" % v.value
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return "nan" if math.isnan(v) else "f" + struct.pack(">d", v).hex()
    if isinstance(v, (bytes, bytearray)):
        return "h'" + bytes(v).hex() + "'"
    if isinstance(v, str):
        return "t'" + v.encode("utf-8", "surrogatepass").hex() + "'"
    if isinstance(v, (list, tuple)):
        return "[" + ",".join(dump(e) for e in v) + "]"
    if isinstance(v, dict):
        return "{" + ",".join(sorted(dump(k) + ":" + dump(x) for k, x in v.items())) + "}"
    if isinstance(v, cbor2.CBORTag):
        return "%d(%s)" % (v.tag, dump(v.value))
    return "<%s:%r>" % (type(v).__name__, v)


def result(h):
    if h == "-":
        return "-"
    try:
        return dump(cbor2.loads(bytes.fromhex(h)))
    except Exception as e:
        return "!" + (type(e).__name__ + ": " + str(e)).replace("\t", " ").replace("\n", " ")


for line in sys.stdin:
    f = line.rstrip("\n").split("\t")
    if len(f) != 4 or f[2].startswith("!"):
        continue
    print(f[0], result(f[2]), result(f[3]), sep="\t")
