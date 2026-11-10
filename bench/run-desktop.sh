#!/bin/bash
# usage: run-desktop.sh <commit-a> <commit-b>
# Measures READ, the lazy path (MB_PATH), at_path (MB_AT_PATH) and the struct decode (MB_STRUCT, twitter only)
# on twitter, floats and records for two commits, with g++ and clang++. See README.md.
set -eu
[ $# -eq 2 ] || { echo "usage: $0 <commit-a> <commit-b>" >&2; exit 2; }
here=$(cd "$(dirname "$0")" && pwd)
. "$here/round.sh"
. "$here/host.sh"
. "$here/gate.sh"
refuse_root
commit_a=$1
commit_b=$2
repo=${REPO:-$(dirname "$here")}
bench=$repo/bench
GXX=${GXX:-$(command -v g++-16 || command -v g++)}
CLANGXX=${CLANGXX:-$(command -v clang++-23 || command -v clang++)}
ROUNDS=${ROUNDS:-10}
MIN_TIME=${MIN_TIME:-0.2s}
docs=${DOCS:-"twitter floats records"}
arms=${ARMS:-"READ MB_PATH MB_AT_PATH MB_STRUCT"}
RESULTS_DIR=${RESULTS_DIR:-$bench/results}
flags=${FLAGS:--O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie -DNDEBUG}
export GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824:glibc.malloc.mmap_threshold=33554432:glibc.malloc.top_pad=268435456

for tool in vmstat ps nice readelf ldd python3 tar; do
	command -v "$tool" > /dev/null || { echo "missing tool: $tool" >&2; exit 2; }
done

build=$(mktemp -d "${TMPDIR:-/tmp}/mbedded-cbor-desktop.XXXXXX")
trap 'rm -rf "${build:?}"' EXIT
mkdir -p "$build/bin" "$build/out" "$build/log" "$build/src" "$build/docs"
if ! gate > "$build/gate-before-build" 2>&1; then
	cat "$build/gate-before-build"
	echo "the machine is busy; no run was made and no file was written" >&2
	exit 3
fi
cat "$build/gate-before-build"
write_host_context "$build/host"
echo "building into $build"

# The tree of a commit: include/ and the bench code of that commit, so each arm builds its own API. COMMIT_TREES=<dir>
# holds <commit>/ (include, bench and test/owned_copy.hpp) and <commit>.id where git is absent.
tree_of() {
	local c=$1 dest=$2
	mkdir -p "$dest"
	if [ -n "${COMMIT_TREES:-}" ]; then
		cp -r "$COMMIT_TREES/$c/." "$dest/"
		cat "$COMMIT_TREES/$c.id" > "$dest/id"
	else
		git -C "$repo" archive "$c" include bench/runtime.cpp bench/value.hpp $(git -C "$repo" ls-tree --name-only "$c" test/owned_copy.hpp) | tar -x -C "$dest"
		git -C "$repo" rev-parse --verify "$c^{commit}" > "$dest/id"
	fi
}
tree_of "$commit_a" "$build/src/a"
tree_of "$commit_b" "$build/src/b"
cp -r "$build/src/a" "$build/src/aa"

# Documents: the repo ones.
for d in $docs; do
	cp "$bench/docs/$d.cbor" "$build/docs/$d.cbor"
done
cp "$bench/runtime.cpp" "$build/runtime.cpp"

compile() {
	local cc=$1 variant=$2 tag=$3 doc=$4 arm=$5
	local name=$variant.$tag.$doc.$arm std=-std=c++23
	local defs=-DARM_"$arm"
	[ "$arm" = MB_STRUCT ] && std="-std=c++26 -freflection"
	case $arm in
	AP_*_[0-9]) defs="-DARM_${arm%_*} -DAP_PATH=${arm##*_}" ;;
	esac
	if "$cc" $std $flags -DDOCTEST_CONFIG_DISABLE $defs -DARM_NAME="\"$arm\"" -DDOC_"$doc" \
		-DDOC_PATH="\"$build/docs/$doc.cbor\"" -I"$build/src/$variant/include" -I"$build/src/$variant/bench" \
		"$build/src/$variant/bench/runtime.cpp" -lbenchmark -lpthread -o "$build/bin/$name" > "$build/log/$name" 2>&1; then
		rm -f "${build:?}/log/${name:?}"
	else
		rm -f "${build:?}/bin/${name:?}"
	fi
}
for cc_tag in gxx:"$GXX" clang:"$CLANGXX"; do
	tag=${cc_tag%%:*}
	cc=${cc_tag#*:}
	for variant in a aa b; do
		for doc in $docs; do
			for arm in $arms; do
				case $arm in
				*:*) [ "${arm%%:*}" = "$doc" ] || continue; arm=${arm#*:} ;;
				esac
				[ "$arm" = MB_STRUCT ] && [ "$doc" != twitter ] && continue
				spawn compile "$cc" "$variant" "$tag" "$doc" "$arm"
			done
		done
	done
done
finish
built=$(ls "$build/bin" | wc -l)
echo "built $built binaries; $(ls "$build/log" | wc -l) did not compile"

# Priority: nice -10 where the system allows it.
runner=$(ask_priority "$build/out")
echo "priority: $(cat "$build/out/priority")"

for b in "$build"/bin/*; do
	[ -e "$b" ] || continue
	echo "$(basename "$b") $(readelf -h "$b" | grep -o 'Type: *[A-Z]*' | tr -s ' ')"
done > "$build/out/elf_type"

# Cells: tag.doc.arm. Per round, the variants run one after the other in a rotating order.
cells=$(cd "$build/bin" && ls | sed -e 's/^[^.]*\.//' | sort -u)
variants=(a b aa)
one_round() {
	local try=$1 attempt=$2 cell k v b
	for cell in $cells; do
		k=0
		while [ "$k" -lt 3 ]; do
			v=${variants[$(((k + attempt) % 3))]}
			b=$build/bin/$v.$cell
			if [ -e "$b" ]; then
				$runner "$b" --benchmark_min_time="$MIN_TIME" --benchmark_out_format=json \
					--benchmark_out="$try/$v.$cell.json" < /dev/null > /dev/null 2>> "$build/out/err.txt" ||
					echo "$v.$cell" >> "$build/out/failed.txt"
			fi
			k=$((k + 1))
		done
	done
}
if ! gate > "$build/gate-before-runs" 2>&1; then
	cat "$build/gate-before-runs"
	echo "the machine is busy after the build; no run was made and no file was written" >&2
	exit 3
fi
run_rounds "$ROUNDS" "$build/out" one_round

mkdir -p "$RESULTS_DIR"
stamp=$(date -u +%Y-%m-%d-%H%M%SZ)
if git -C "$repo" rev-parse --is-inside-work-tree > "$build/out/git.log" 2>&1; then
	bench_commit=$(name_bench_commit "$repo" bench cmake)
else
	bench_commit=${BENCH_COMMIT:-unknown}
fi
python3 -I "$here/context.py" "$build" "$steal_limit_percent" "$GXX" "$CLANGXX" -- \
	$(ls "$build"/bin/a.gxx.* 2>> "$build/out/err.txt" | head -n 1) \
	$(ls "$build"/bin/a.clang.* 2>> "$build/out/err.txt" | head -n 1) > "$build/out/context.json"
python3 -I - "$build" "$stamp" "$commit_a" "$commit_b" "$bench_commit" "$flags" "$ROUNDS" "$MIN_TIME" \
	> "$RESULTS_DIR/$stamp.json.part" <<'PY'
import collections, glob, json, os, statistics, sys

build, stamp, commit_a, commit_b, bench_commit, flags, rounds, min_time = sys.argv[1:9]


def read(path):
    return open(path).read().strip() if os.path.exists(path) else ""


def ids(v):
    return read(os.path.join(build, "src", v, "id"))


times = collections.defaultdict(list)
context = {}
for path in sorted(glob.glob(os.path.join(build, "out", "round*", "*.json"))):
    v, cell = os.path.basename(path)[:-5].split(".", 1)
    data = json.load(open(path))
    context.setdefault((v, cell), data["context"])
    times[(v, cell)] += [b["real_time"] for b in data["benchmarks"]]

cells = sorted({cell for _, cell in times} | {f[len("a."):] for f in os.listdir(os.path.join(build, "log")) if f.startswith("a.")})
errors = {f: read(os.path.join(build, "log", f))[:2000] for f in os.listdir(os.path.join(build, "log"))}
result = {}
for cell in cells:
    row = {}
    for v in ("a", "aa", "b"):
        t = times.get((v, cell))
        if t:
            row[v] = {"repetitions_ns": t, "median_ns": statistics.median(t), "spread": max(t) / min(t) - 1,
                      "check": context[(v, cell)].get("check"), "copies": context[(v, cell)].get("copies")}
        else:
            row[v] = {"error": errors.get(f"{v}.{cell}", "no result")}
    if all("median_ns" in row[v] for v in row):
        row["a_over_a_ratio"] = row["aa"]["median_ns"] / row["a"]["median_ns"]
        row["b_over_a_ratio"] = row["b"]["median_ns"] / row["a"]["median_ns"]
        row["same_check"] = row["a"]["check"] == row["b"]["check"]
        floor = abs(row["a_over_a_ratio"] - 1)
        row["difference_over_noise"] = abs(row["b_over_a_ratio"] - 1) > floor
    result[cell] = row
aa_dev = [abs(r["a_over_a_ratio"] - 1) for r in result.values() if "a_over_a_ratio" in r]
context = json.load(open(os.path.join(build, "out", "context.json")))
binaries = context.pop("binaries")
print(json.dumps({
    "time": stamp,
    "commit_a": {"given": commit_a, "id": ids("a")},
    "commit_b": {"given": commit_b, "id": ids("b")},
    "bench_sources_commit": bench_commit,
    "what": os.environ.get("WHAT", "READ, lazy path (MB_PATH), at_path (MB_AT_PATH) and struct decode (MB_STRUCT)") + "; a = commit a, aa = commit a built again, b = commit b",
    "documents": os.environ.get("DOCS", "twitter floats records") + "; MB_STRUCT on twitter only",
    "flags": flags,
    "pie": "-fPIE -pie" if {"-fPIE", "-pie"} <= set(flags.split()) else "default of the compiler",
    "elf_type_count": dict(collections.Counter(l.split(" ", 1)[1] for l in read(os.path.join(build, "out", "elf_type")).splitlines())),
    "rounds": int(rounds),
    "min_time": min_time,
    "gxx_binary": next((b for n, b in binaries.items() if n.startswith("a.gxx.")), {}),
    "clang_binary": next((b for n, b in binaries.items() if n.startswith("a.clang.")), {}),
    **context,
    "noise_floor": {"a_over_a_max_deviation_percent": 100 * max(aa_dev, default=0)},
    "steal_percent_of_cpu_time_max": max((r["steal_percent_of_cpu_time"] for r in context["rounds_detail"]), default=None),
    "cells": result,
}, indent=1))
PY
mv "$RESULTS_DIR/$stamp.json.part" "$RESULTS_DIR/$stamp.json"
echo "wrote $RESULTS_DIR/$stamp.json"
python3 -I - "$RESULTS_DIR/$stamp.json" <<'PY'
import json, sys
d = json.load(open(sys.argv[1]))
print("noise floor (max A/A deviation): %.2f%%" % d["noise_floor"]["a_over_a_max_deviation_percent"])
print("cell: B/A, A/A (median of %d rounds; * = B/A differs by more than the A/A of its cell)" % d["rounds"])
for cell, r in d["cells"].items():
    if "b_over_a_ratio" in r:
        print("%s: %.3f%s, %.3f" % (cell, r["b_over_a_ratio"], "*" if r["difference_over_noise"] else "", r["a_over_a_ratio"]))
    else:
        print("%s: %s" % (cell, "; ".join(k + " " + v["error"].splitlines()[0] for k, v in r.items() if isinstance(v, dict) and "error" in v)))
PY
