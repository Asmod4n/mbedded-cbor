import json
import sys

if len(sys.argv) != 2:
    sys.exit("usage: python3 bench/show.py bench/results/<file>.json")

data = json.load(open(sys.argv[1]))
arms = data["arms"]
print(f'{data["time"]}  {data["commit"]}  {data["flags"]}  {data["cpu"]["model"]}  {data["processes"]} processes')
print()

names = {"S": "mbedded-cbor", "READ": "mbedded-cbor", "LC": "libcbor", "JC": "jsoncons", "VG": "vladimirgamalyan",
         "MP": "msgpack-cxx", "FB": "FlexBuffers"}
docs = ["cwt", "senml", "floats", "ints", "strings", "records", "twitter"]
compilers = sorted({n.split(".")[1] for n in arms if n.startswith("rt.")})
for job, kinds in (("encode", ("S", "_PREALLOC", "_CLEAR", "_RAW", "_REUSE")), ("read every item", ("READ", "_READ"))):
    for cc in compilers:
        print(f"== runtime, {job}, {cc}: documents per second, best first")
        for d in docs:
            row = []
            for n, a in arms.items():
                parts = n.split(".")
                if parts[0] != "rt" or parts[1] != cc or parts[3] != d:
                    continue
                arm = parts[2]
                if arm not in kinds and not any(arm.endswith(k) for k in kinds if k.startswith("_")):
                    continue
                row.append((1e9 / a["median_ns"], names[arm.split("_")[0]]))
            row.sort(reverse=True)
            top = row[0][0] if row else 1
            cells = "  ".join(f"{lib} {v:,.0f} ({v / top * 100:.0f}%)" for v, lib in row)
            print(f"  {d:8} {cells}")
        print()

cw = {n[3:]: a for n, a in arms.items() if n.startswith("cw.")}
if cw:
    print("== cwt through cbor::databind, ns per document")
    for op, a in sorted(cw.items()):
        print(f"  {op:6} {a['median_ns']:8.1f}")
    print()

schema = {n[3:]: a for n, a in arms.items() if n.startswith("sc.")}
if schema:
    print("== schema, carsales, ns per car (lower is better)")
    label = {"ENC": "mbedded-cbor encode", "FB_ENC": "FlatBuffers encode", "CP_ENC": "Cap'n Proto encode",
             "PATH": "mbedded-cbor read (path)", "DEC": "mbedded-cbor read (decode)",
             "FB_READ": "FlatBuffers read (verify)", "CP_READ": "Cap'n Proto read"}
    for op in ("ENC", "FB_ENC", "CP_ENC", "PATH", "DEC", "FB_READ", "CP_READ"):
        if op in schema:
            print(f"  {label[op]:28} {schema[op]['median_ns'] / 1000:8.1f}")
    print()

if data.get("failed"):
    print("failed:", " ".join(data["failed"]))
