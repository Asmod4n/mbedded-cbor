# cbor2: the canonical dump of one top-level item, the same text as dump.hpp. cbor2 gives a tag that it does
# not know as CBORTag (RFC 8949 3.4), resolves tags 28 and 29, and turns tags 2 and 3 into an int. A datetime is
# the tag 0 and the text that cbor2.dumps writes for it, and a frozendict is a map, as cbor2.dumps writes it.
# With no argument it prints "name TAB dump-of-written TAB dump-of-expected" for each line of cases.tsv. With
# "reencode" it writes the value of the input of each case again with cbor2.dumps and prints "name TAB input TAB
# written TAB dump-of-written", as dump.hpp does. With "cycles-write" it prints the cases of cycles.tsv that
# cbor2.dumps writes with value_sharing=True. With "cycles-read" it prints "name TAB graph" for each line of
# cycles.tsv, in the text of read.cpp.
import datetime
import math
import struct
import sys

import cbor2

DATETIME_TAG = 0
CONTAINERS = (list, tuple, dict, cbor2.frozendict, cbor2.CBORTag)


def children(v):
    if isinstance(v, (list, tuple)):
        return list(v)
    if isinstance(v, (dict, cbor2.frozendict)):
        return [x for pair in v.items() for x in pair]
    if isinstance(v, cbor2.CBORTag):
        return [v.value]
    return []


def targets(root):
    seen, named, todo = set(), set(), [root]
    while todo:
        v = todo.pop()
        if not isinstance(v, CONTAINERS):
            continue
        if id(v) in seen:
            named.add(id(v))
            continue
        seen.add(id(v))
        todo.extend(children(v))
    return named


def dump(v, named=frozenset(), numbered=None, key=False):
    if isinstance(v, CONTAINERS) and id(v) in named:
        if key:
            return "*"
        if id(v) in numbered:
            return "*%d" % numbered[id(v)]
        numbered[id(v)] = len(numbered)
        return "&%d" % numbered[id(v)] + content(v, named, numbered, False)
    return content(v, named, numbered, key)


def content(v, named, numbered, key):
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
        return "[" + ",".join(dump(e, named, numbered, key) for e in v) + "]"
    if isinstance(v, (dict, cbor2.frozendict)):
        if key or not named:
            return "{" + ",".join(sorted(dump(k, named, numbered, True) + ":" + dump(x, named, numbered, True)
                                         for k, x in v.items())) + "}"
        pairs = sorted(v.items(), key=lambda p: dump(p[0], named, numbered, True) + ":" +
                       dump(p[1], named, numbered, True))
        return "{" + ",".join(dump(k, named, numbered) + ":" + dump(x, named, numbered) for k, x in pairs) + "}"
    if isinstance(v, cbor2.CBORTag):
        return "%d(%s)" % (v.tag, dump(v.value, named, numbered, key))
    if isinstance(v, datetime.datetime):
        text = v.isoformat()
        if text.endswith("+00:00"):
            text = text[:-len("+00:00")] + "Z"
        return "%d(t'%s')" % (DATETIME_TAG, text.encode("utf-8").hex())
    return "<%s:%r>" % (type(v).__name__, v)


def graph(v):
    return dump(v, targets(v), {})


def one_line(e):
    return "!" + (type(e).__name__ + ": " + str(e)).replace("\t", " ").replace("\n", " ")


def result(h):
    if h == "-":
        return "-"
    try:
        return dump(cbor2.loads(bytes.fromhex(h)))
    except Exception as e:
        return one_line(e)


def reencode(h):
    try:
        v = cbor2.loads(bytes.fromhex(h))
    except Exception:
        return None
    try:
        written = cbor2.dumps(v).hex()
    except Exception as e:
        return h, one_line(e), "-"
    return h, written, result(written)


def cycles_write():
    itself = []
    itself.append(itself)
    print("cbor2/list_holds_itself", cbor2.dumps(itself, value_sharing=True).hex(), "&0[*0]", "cycle", sep="\t")
    holder = {}
    holder["a"] = holder
    print("cbor2/dict_holds_itself", cbor2.dumps(holder, value_sharing=True).hex(), "&0{t'61':*0}", "cycle",
          sep="\t")
    x = [1]
    print("cbor2/one_list_twice", cbor2.dumps([x, x], value_sharing=True).hex(), "[&0[1],*0]", "sharing", sep="\t")


def cycles_read():
    for line in sys.stdin:
        name, written, _, _ = line.rstrip("\n").split("\t")
        try:
            got = graph(cbor2.loads(bytes.fromhex(written)))
        except Exception as e:
            got = one_line(e)
        print(name, got, sep="\t")


mode = sys.argv[1] if len(sys.argv) > 1 else ""
if mode == "cycles-write":
    cycles_write()
elif mode == "cycles-read":
    cycles_read()
elif mode not in ("", "reencode"):
    sys.exit("usage: cbor2_dump.py [reencode|cycles-write|cycles-read] <lines")
else:
    for line in sys.stdin:
        f = line.rstrip("\n").split("\t")
        if len(f) != 4 or f[2].startswith("!"):
            continue
        if mode == "reencode":
            r = reencode(f[3] if f[3] != "-" else f[2])
            if r:
                print(f[0], *r, sep="\t")
        else:
            print(f[0], result(f[2]), result(f[3]), sep="\t")
