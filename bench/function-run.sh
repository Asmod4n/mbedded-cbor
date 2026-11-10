#!/bin/bash
# The part of bench/function.sh that runs in the container: "build" makes the binaries, "measure" runs them and writes
# /run-dir/out/result.json.
set -eu
dir=/run-dir
flags=${FLAGS:-"-std=c++26 -O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -DNDEBUG -pipe"}
link_flags="-pie -fuse-ld=mold"
compilers="gxx:g++-16 clang:clang++-23"
variants="a aa b"
export GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824:glibc.malloc.mmap_threshold=33554432:glibc.malloc.top_pad=268435456
. /bench/round.sh
. /cmake/warnings.sh

pch_of() {
	local tag=$1 cc=$2 key header warnings
	warnings=$(warnings_for "$cc")
	key=$( { cat "$dir/std.hpp"; "$cc" --version; echo "$flags $warnings"; "$cc" -march=native -### -E -x c++ - < /dev/null 2>&1 | grep -oE '(-march=[a-z0-9_-]+|"-target-cpu" "[a-z0-9_-]+")'; } |
		sha256sum | cut -c1-16)
	header=/pch/$tag-$key/std.hpp
	if [ ! -e "$header.done" ]; then
		mkdir -p "/pch/$tag-$key"
		cp "$dir/std.hpp" "$header"
		case $tag in
		gxx) "$cc" $flags $warnings -x c++-header "$header" -o "$header.gch" ;;
		clang) "$cc" $flags $warnings -Xclang -fno-pch-timestamp -x c++-header "$header" -o "$header.pch" ;;
		esac
		touch "$header.done"
	fi
	case $tag in
	gxx) echo "-include $header -Winvalid-pch" ;;
	clang) echo "-Xclang -fno-pch-timestamp -include-pch $header.pch" ;;
	esac
}

compile() {
	local tag=$1 cc=$2 v=$3 pch=$4 src=$v
	[ "$v" = aa ] && src=a
	mkdir -p "$dir/bin" "$dir/log"
	if ccache "$cc" $flags $(warnings_for "$cc") $pch -I"$dir/src/$src/include" -c "$dir/bench.cpp" -o "$dir/bin/$v.$tag.o" > "$dir/log/$v.$tag" 2>&1 &&
		"$cc" $flags $link_flags "$dir/bin/$v.$tag.o" -lbenchmark -lpthread -o "$dir/bin/$v.$tag" >> "$dir/log/$v.$tag" 2>&1; then
		rm -f "${dir:?}/log/${v:?}.${tag:?}" "${dir:?}/bin/${v:?}.${tag:?}.o"
	else
		rm -f "${dir:?}/bin/${v:?}.${tag:?}" "${dir:?}/bin/${v:?}.${tag:?}.o"
	fi
}

build() {
	local start end tag cc pch
	start=$EPOCHREALTIME
	{ cat "$dir"/src/a/include/cbor/*.hpp "$dir"/src/b/include/cbor/*.hpp | grep -oE '^#include <[a-z_]+>' | sort -u
	  echo '#include <benchmark/benchmark.h>'; } > "$dir/std.hpp"
	for each in $compilers; do
		tag=${each%%:*}
		cc=${each#*:}
		pch=$(pch_of "$tag" "$cc")
		for v in $variants; do
			spawn compile "$tag" "$cc" "$v" "$pch"
		done
	done
	finish
	end=$EPOCHREALTIME
	python3 -I -c 'import sys; print("build: %.2f s for %s binaries" % (float(sys.argv[2]) - float(sys.argv[1]), sys.argv[3]))' \
		"$start" "$end" "$(ls "$dir/bin" | wc -l)" | tee "$dir/out/build"
	for f in "$dir"/log/*; do
		[ -e "$f" ] || continue
		echo "did not compile: $(basename "$f")"
		head -n "$error_lines_shown" "$f"
	done
}

one_round() {
	local try=$1 attempt=$2 order b
	order=$(echo "$binaries" | awk -v r="$attempt" '{ l[NR] = $0 } END { for (j = 0; j < NR; j++) print l[(j + r) % NR + 1] }')
	for b in $order; do
		$runner "$dir/bin/$b" --benchmark_min_time="$MIN_TIME" --benchmark_out_format=json \
			--benchmark_out="$try/$b.json" < /dev/null > /dev/null 2>> "$dir/out/err.txt" ||
			echo "$b" >> "$dir/out/failed.txt"
	done
}

measure() {
	runner=$(ask_priority "$dir/out")
	echo "priority: $(cat "$dir/out/priority")"
	binaries=$(cd "$dir/bin" && ls)
	run_rounds "$ROUNDS" "$dir/out" one_round
	python3 -I /bench/context.py "$dir" "$steal_limit_percent" g++-16 clang++-23 -- "$dir/bin/a.gxx" "$dir/bin/a.clang" \
		> "$dir/out/context.json"
	python3 -I - "$dir" "$flags" "$(warnings_for g++-16 | tr '\n' ' ')" "$(warnings_for clang++-23 | tr '\n' ' ')" \
		> "$dir/out/result.json" <<'PY'
import collections, glob, json, os, statistics, subprocess, sys

d, flags, warnings_gxx, warnings_clang = sys.argv[1:5]
env = os.environ
utest_alpha = 0.05
machine_floor = float(env["NOISE_FLOOR_PERCENT"]) / 100


def run(*cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return (r.stdout + r.stderr).strip()


def read(path):
    return open(path).read().strip() if os.path.exists(path) else ""


times = collections.defaultdict(list)
skipped = {}
merged = {}
for path in sorted(glob.glob(os.path.join(d, "out", "round*", "*.json"))):
    v, tag = os.path.basename(path)[:-5].split(".")
    data = json.load(open(path))
    merged.setdefault((tag, v), {"context": data["context"], "benchmarks": []})
    for b in data["benchmarks"]:
        if b.get("error_occurred"):
            skipped[(tag, v, b["name"])] = b.get("error_message", "")
            continue
        merged[(tag, v)]["benchmarks"].append(b)
        times[(tag, b["name"], v)].append(b["real_time"] * {"ns": 1, "us": 1e3, "ms": 1e6, "s": 1e9}[b["time_unit"]])
for (tag, v), data in merged.items():
    json.dump(data, open(os.path.join(d, "out", f"merged.{v}.{tag}.json"), "w"))


def utest(tag, first, second):
    a = os.path.join(d, "out", f"merged.{first}.{tag}.json")
    b = os.path.join(d, "out", f"merged.{second}.{tag}.json")
    dump = os.path.join(d, "out", f"compare.{first}.{second}.{tag}.json")
    if not (os.path.exists(a) and os.path.exists(b)):
        return {}
    text = run("python3", "/usr/share/benchmark/compare.py", "--no-color", "-d", dump, "benchmarks", a, b)
    result = {"text": text}
    if os.path.exists(dump):
        for entry in json.load(open(dump)):
            if entry.get("utest"):
                result[entry["name"]] = entry["utest"]
    return result


utests = {tag: {"a_aa": utest(tag, "a", "aa"), "a_b": utest(tag, "a", "b")} for tag in ("gxx", "clang")}

error_text_bytes = 2000
errors = {f: read(os.path.join(d, "log", f))[:error_text_bytes] for f in os.listdir(os.path.join(d, "log"))} if os.path.isdir(os.path.join(d, "log")) else {}
functions = sorted({name for _, name, _ in times} | {name for _, _, name in skipped})
cells = {}
for tag in ("gxx", "clang"):
    for name in functions or ["(none)"]:
        row = {}
        for v in ("a", "aa", "b"):
            t = times.get((tag, name, v))
            if t:
                row[v] = {"repetitions_ns": t, "median_ns": statistics.median(t), "spread": max(t) / min(t) - 1}
            else:
                row[v] = {"error": skipped.get((tag, v, name)) or errors.get(f"{v}.{tag}", "no result")}
        if all("median_ns" in row[v] for v in ("a", "aa", "b")):
            row["a_over_a_ratio"] = row["aa"]["median_ns"] / row["a"]["median_ns"]
            row["b_over_a_ratio"] = row["b"]["median_ns"] / row["a"]["median_ns"]
            for pair in ("a_aa", "a_b"):
                u = utests[tag][pair].get(name)
                if u:
                    row["utest_" + pair] = {"time_pvalue": u["time_pvalue"], "repetitions": u["nr_of_repetitions"],
                                            "optimal_repetitions": u["have_optimal_repetitions"]}
            u = row.get("utest_a_b")
            row["difference_over_noise"] = (bool(u) and u["time_pvalue"] < utest_alpha and
                                            abs(row["b_over_a_ratio"] - 1) > max(abs(row["a_over_a_ratio"] - 1), machine_floor))
        cells.setdefault(tag, {})[name] = row

context = json.load(open(os.path.join(d, "out", "context.json")))
binaries = context.pop("binaries")
aa_dev = [abs(r["a_over_a_ratio"] - 1) for c in cells.values() for r in c.values() if "a_over_a_ratio" in r]
print(json.dumps({
    "time": env["STAMP"],
    "block": env["BLOCK"],
    "what": "each function of bench/function/%s.cpp over a buffer of COLD_BYTES bytes of items; a = commit a, aa = commit a built again, b = commit b" % env["BLOCK"],
    "commit_a": {"given": env["COMMIT_A"], "id": read(os.path.join(d, "src", "a", "id"))},
    "commit_b": {"given": env["COMMIT_B"], "id": read(os.path.join(d, "src", "b", "id"))},
    "bench_sources_commit": env["BENCH_COMMIT"],
    "image": env["IMAGE"],
    "flags": flags,
    "warnings": {"gxx": warnings_gxx.strip(), "clang": warnings_clang.strip()},
    "link_flags": env.get("LINK_FLAGS_USED", "-pie -fuse-ld=mold"),
    "gxx_binary": binaries.get("a.gxx", {}),
    "clang_binary": binaries.get("a.clang", {}),
    "rounds": int(env["ROUNDS"]),
    "min_time": env["MIN_TIME"],
    "cold_bytes": int(env["COLD_BYTES"]),
    "build": read(os.path.join(d, "out", "build")),
    **context,
    "noise_floor": {"a_over_a_max_deviation_percent": 100 * max(aa_dev, default=0)},
    "difference_test": "two-tailed Mann-Whitney U test of compare.py of Google Benchmark over the rounds, alpha %s, and B/A further from 1 than A/A and than the noise floor of the machine (%s %%)" % (utest_alpha, env["NOISE_FLOOR_PERCENT"]),
    "compare_text": {tag: {pair: u.get("text", "") for pair, u in p.items()} for tag, p in utests.items()},
    "cells": cells,
}, indent=1))
PY
}

"$1"
