# Writes the result file of bench/readme-table.sh and prints the table "Performance" of README.md.
# usage: python3 -I readme-table.py <run-dir> <result-file> <commit>
import glob
import json
import os
import re
import statistics
import sys

run, file, commit = sys.argv[1:4]
out = os.path.join(run, "out")
context = json.load(open(os.path.join(out, "context.json")))
context.update(json.load(open(os.path.join(out, "settings.json"))))
context["commit"] = commit
runs = {}
for f in glob.glob(os.path.join(out, "round*", "*.json")):
    r = int(re.search(r"round(\d+)$", os.path.dirname(f)).group(1))
    arm = os.path.basename(f)[:-5].split(".", 1)[1]
    b = [x for x in json.load(open(f))["benchmarks"] if x.get("run_type", "iteration") == "iteration"][0]
    runs.setdefault(arm, {})[r] = (b["real_time"], b["time_unit"])
res = {}
for arm, v in sorted(runs.items()):
    t = [v[i][0] for i in sorted(v)]
    res[arm] = {"unit": v[min(v)][1], "repetitions": t, "median": statistics.median(t), "spread": max(t) / min(t) - 1}
json.dump({"what": "README table: carsales, compare.cpp", "context": context, "results": res}, open(file, "w"), indent=1)
scale = {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}


def show(arm):
    r = res[arm]
    t = r["median"] * scale[r["unit"]]
    value = next(f"{t / f:.1f} {u}" for f, u in [(1e9, "s"), (1e6, "ms"), (1e3, "µs"), (1, "ns")] if t >= f or u == "ns")
    return f"{value} (±{r['spread'] * 100:.0f} %)"


print(f"\n{'task':<14} {'mbedded-cbor':<20} {'FlatBuffers+Verifier':<22} Cap'n Proto")
for task, a, b, c in [("one field", "MB_FIELD", "FB_FIELD_V", "CP_FIELD"), ("whole message", "MB_READ", "FB_READ_V", "CP_READ"),
                      ("write", "MB_ENCODE", "FB_ENCODE", "CP_ENCODE")]:
    print(f"{task:<14} {show(a):<20} {show(b):<22} {show(c)}")
print(f"noise (A/A of one field): {res['MB_FIELD_AA']['median'] / res['MB_FIELD']['median'] - 1:+.1%}")
print(f"\nwritten: {file}")
