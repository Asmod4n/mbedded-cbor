# Writes the result file of bench/kinds-table.sh and prints the kinds table of each compiler.
# usage: python3 -I kinds-table.py <run-dir> <result-file> <commit> <field arms> <whole arms> <write arms>
import glob
import json
import os
import re
import statistics
import sys

run, file, commit, field, whole, write = sys.argv[1:7]
out = os.path.join(run, "out")
context = json.load(open(os.path.join(out, "context.json")))
context.update(json.load(open(os.path.join(out, "settings.json"))))
context["commit"] = commit
runs, checks = {}, {}
for f in glob.glob(os.path.join(out, "round*", "*.json")):
    r = int(re.search(r"round(\d+)$", os.path.dirname(f)).group(1))
    b = os.path.basename(f)[:-5]
    d = json.load(open(f))
    x = [y for y in d["benchmarks"] if y.get("run_type", "iteration") == "iteration"][0]
    runs.setdefault(b, {})[r] = (x["real_time"], x["time_unit"])
    checks[b] = d["context"]
bad = []
res = {}
for b, v in sorted(runs.items()):
    c = checks[b]
    arm = b.split(".", 1)[1]
    want = float(c["check_field"]) if arm in field.split() else float(c["check_source"])
    got = float(c["check"])
    if abs(got - want) > 1e-9 * abs(want):
        bad.append(f"{b}: check {got!r}, expected {want!r}")
    t = [v[i][0] for i in sorted(v)]
    res[b] = {"unit": v[min(v)][1], "repetitions": t, "median": statistics.median(t), "spread": max(t) / min(t) - 1,
              "check": got, "expected": want, "bytes_written": int(c.get("bytes_written", "0")),
              "bytes_plain": int(c["bytes_plain"]), "bytes_packed": int(c["bytes_mb"])}
json.dump({"what": "kinds table: carsales (bench/cars.json), bench/compare.cpp, bench/kinds-table.sh", "context": context,
           "results": res, "check_failures": bad}, open(file, "w"), indent=1)
print(f"written: {file}")
if bad:
    print("\n".join(bad))
    sys.exit(1)
scale = {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}


def ns(b):
    return res[b]["median"] * scale[res[b]["unit"]]


for cxx in sorted({b.split(".", 1)[0] for b in res}):
    print(f"\n{cxx}")
    for task, arms in [("field", field), ("whole", whole), ("write", write)]:
        a = arms.split()
        aa = [x for x in a if x.endswith("_AA")][0]
        if f"{cxx}.{aa}" in res and f"{cxx}.{aa[:-3]}" in res:
            print(f"  {task}: noise (A/A) {ns(f'{cxx}.{aa}') / ns(f'{cxx}.{aa[:-3]}') - 1:+.1%}")
        for x in a:
            if f"{cxx}.{x}" in res:
                print(f"    {x:<22} {ns(f'{cxx}.{x}'):>14.0f} ns ±{res[f'{cxx}.{x}']['spread'] * 100:.0f} %  bytes_written {res[f'{cxx}.{x}']['bytes_written']}")
