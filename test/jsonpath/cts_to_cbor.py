# Writes cts.cbor from cts.json of the JSONPath Compliance Test Suite.
# The library has no public function that turns EDN into CBOR, so the
# JSON of each case is turned into CBOR here, once, before the tests run.
# cts.cbor is one array. A valid case is [name, selector, document,
# results], where results is the array of every allowed nodelist. A case
# with an invalid selector is [name, selector].
# Usage: python3 -I cts_to_cbor.py cts.json cts.cbor
import json
import struct
import sys


def head(major, n):
    if n < 24:
        return bytes([major << 5 | n])
    for info, fmt in ((24, ">B"), (25, ">H"), (26, ">I"), (27, ">Q")):
        if n < 1 << (8 * struct.calcsize(fmt)):
            return bytes([major << 5 | info]) + struct.pack(fmt, n)
    raise ValueError(n)


def encoded(v):
    if v is None:
        return b"\xf6"
    if v is True:
        return b"\xf5"
    if v is False:
        return b"\xf4"
    if isinstance(v, int):
        return head(0, v) if v >= 0 else head(1, -1 - v)
    if isinstance(v, float):
        for info, fmt in ((25, ">e"), (26, ">f")):
            try:
                if struct.unpack(fmt, struct.pack(fmt, v))[0] == v:
                    return bytes([0xe0 | info]) + struct.pack(fmt, v)
            except OverflowError:
                pass
        return b"\xfb" + struct.pack(">d", v)
    if isinstance(v, str):
        b = v.encode("utf-8", "surrogatepass")
        return head(3, len(b)) + b
    if isinstance(v, list):
        return head(4, len(v)) + b"".join(encoded(e) for e in v)
    if isinstance(v, dict):
        return head(5, len(v)) + b"".join(encoded(k) + encoded(e) for k, e in v.items())
    raise TypeError(v)


cases = []
for c in json.load(open(sys.argv[1], encoding="utf-8"))["tests"]:
    if c.get("invalid_selector"):
        cases.append([c["name"], c["selector"]])
    else:
        cases.append([c["name"], c["selector"], c["document"], c["results"] if "results" in c else [c["result"]]])
open(sys.argv[2], "wb").write(encoded(cases))
