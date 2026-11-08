# Prints the result file of bench/function.sh as one table: a row per function, a column per build, B/A and A/A.
import json
import sys

d = json.load(open(sys.argv[1]))
builds = ["gxx", "clang"]
missing = {"tumbleweed": "image mbedded-cbor-bench-desktop does not build"}
print("%s, %s -> %s, %d rounds, min_time %s, %d bytes per iteration" % (
    d["block"], d["commit_a"]["id"][:10], d["commit_b"]["id"][:10], d["rounds"], d["min_time"], d["cold_bytes"]))
print("noise floor (max A/A deviation): %.2f %%" % d["noise_floor"]["a_over_a_max_deviation_percent"])
print("cell: median of a in ms, B/A, A/A; * = B/A differs from 1 by more than A/A")
print("| function | " + " | ".join(builds + list(missing)) + " |")
print("|---" * (1 + len(builds) + len(missing)) + "|")
for name in sorted({n for b in builds for n in d["cells"].get(b, {})}):
    cells = []
    for b in builds:
        r = d["cells"].get(b, {}).get(name)
        if r and "b_over_a_ratio" in r:
            cells.append("%.3f ms, %.3f%s, %.3f" % (r["a"]["median_ns"] / 1e6, r["b_over_a_ratio"],
                                                    "*" if r["difference_over_noise"] else "", r["a_over_a_ratio"]))
        elif r:
            cells.append("; ".join(v + ": " + r[v]["error"].splitlines()[0] for v in ("a", "aa", "b") if "error" in r[v]))
        else:
            cells.append("no result")
    print("| %s | %s | %s |" % (name, " | ".join(cells), " | ".join(missing.values())))
print(d["build"])
print("priority: %s" % d["priority"])
print("rounds kept %d, discarded %d" % (sum(r["kept"] for r in d["rounds_detail"]), sum(not r["kept"] for r in d["rounds_detail"])))
