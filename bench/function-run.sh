#!/bin/bash
# The part of bench/function.sh that runs in the container: "build" makes the binaries, "measure" runs them and writes
# /run-dir/out/result.json.
set -eu
dir=/run-dir
flags=${FLAGS:-"-std=c++26 -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion -Wold-style-cast -Werror -O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -DNDEBUG -pipe"}
link_flags="-pie -fuse-ld=mold"
compilers="gxx:g++-16 clang:clang++-23"
variants="a aa b"
export GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824:glibc.malloc.mmap_threshold=33554432:glibc.malloc.top_pad=268435456
steal_limit_percent=3

pch_of() {
	local tag=$1 cc=$2 key header
	key=$( { cat "$dir/std.hpp"; "$cc" --version; echo "$flags"; "$cc" -march=native -### -E -x c++ - < /dev/null 2>&1 | grep -oE '(-march=[a-z0-9_-]+|"-target-cpu" "[a-z0-9_-]+")'; } |
		sha256sum | cut -c1-16)
	header=/pch/$tag-$key/std.hpp
	if [ ! -e "$header.done" ]; then
		mkdir -p "/pch/$tag-$key"
		cp "$dir/std.hpp" "$header"
		case $tag in
		gxx) "$cc" $flags -x c++-header "$header" -o "$header.gch" ;;
		clang) "$cc" $flags -Xclang -fno-pch-timestamp -x c++-header "$header" -o "$header.pch" ;;
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
	if ccache "$cc" $flags $pch -I"$dir/src/$src/include" -c "$dir/bench.cpp" -o "$dir/bin/$v.$tag.o" > "$dir/log/$v.$tag" 2>&1 &&
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
			compile "$tag" "$cc" "$v" "$pch" &
		done
	done
	wait
	end=$EPOCHREALTIME
	python3 -I -c 'import sys; print("build: %.2f s for %s binaries" % (float(sys.argv[2]) - float(sys.argv[1]), sys.argv[3]))' \
		"$start" "$end" "$(ls "$dir/bin" | wc -l)" | tee "$dir/out/build"
	for f in "$dir"/log/*; do
		[ -e "$f" ] || continue
		echo "did not compile: $(basename "$f")"
		head -n 20 "$f"
	done
}

steal() {
	local user nice system idle iowait irq softirq stolen rest
	read -r _ user nice system idle iowait irq softirq stolen rest < /proc/stat
	echo "$stolen $((user + nice + system + idle + iowait + irq + softirq + stolen))"
}

measure() {
	local runner= priority i=1 attempt=0 k s0 s1 t0 t1 order
	if [ "$(nice -n -10 nice 2>> "$dir/out/nice.log")" = -10 ]; then
		runner="nice -n -10"
		priority="nice -10 granted"
	else
		priority="nice 0 (nice -10 refused: $(tr '\n' ' ' < "$dir/out/nice.log"))"
	fi
	echo "priority: $priority"
	echo "$priority" > "$dir/out/priority"
	binaries=$(cd "$dir/bin" && ls)
	while [ "$i" -le "$ROUNDS" ] && [ "$attempt" -lt $((2 * ROUNDS)) ]; do
		attempt=$((attempt + 1))
		order=$(echo "$binaries" | awk -v r="$attempt" '{ l[NR] = $0 } END { for (j = 0; j < NR; j++) print l[(j + r) % NR + 1] }')
		s0=$(steal)
		t0=$EPOCHREALTIME
		mkdir -p "$dir/out/try$attempt"
		for b in $order; do
			$runner "$dir/bin/$b" --benchmark_min_time="$MIN_TIME" --benchmark_out_format=json \
				--benchmark_out="$dir/out/try$attempt/$b.json" < /dev/null > /dev/null 2>> "$dir/out/err.txt" ||
				echo "$b" >> "$dir/out/failed.txt"
		done
		s1=$(steal)
		t1=$EPOCHREALTIME
		if python3 -I -c 'import sys; a, b, c, d = map(int, sys.argv[1:5]); sys.exit(0 if 100 * (c - a) <= int(sys.argv[5]) * max(1, d - b) else 1)' \
			$s0 $s1 "$steal_limit_percent"; then
			mv "$dir/out/try$attempt" "$dir/out/round$i"
			echo "$i $attempt $t0 $t1 $s0 $s1 kept" >> "$dir/out/rounds"
			echo "round $i/$ROUNDS kept"
			i=$((i + 1))
		else
			echo "$i $attempt $t0 $t1 $s0 $s1 discarded" >> "$dir/out/rounds"
			echo "round $i/$ROUNDS discarded: steal above $steal_limit_percent %"
		fi
	done
	python3 -I - "$dir" "$flags" "$steal_limit_percent" > "$dir/out/result.json" <<'PY'
import collections, glob, hashlib, json, os, statistics, subprocess, sys

d, flags, steal_limit = sys.argv[1:4]
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

errors = {f: read(os.path.join(d, "log", f))[:2000] for f in os.listdir(os.path.join(d, "log"))} if os.path.isdir(os.path.join(d, "log")) else {}
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

rounds = []
for line in read(os.path.join(d, "out", "rounds")).splitlines():
    i, attempt, t0, t1, st0, tot0, st1, tot1, kept = line.split()
    rounds.append({"round": int(i), "attempt": int(attempt), "wall_s": float(t1) - float(t0), "kept": kept == "kept",
                   "steal_percent_of_cpu_time": 100.0 * (int(st1) - int(st0)) / max(1, int(tot1) - int(tot0))})


def cpu():
    model, fl = "", ""
    for line in open("/proc/cpuinfo"):
        if line.startswith("model name") and not model:
            model = line.split(":", 1)[1].strip()
        if line.startswith("flags") and not fl:
            fl = line.split(":", 1)[1].strip()
    return {"model": model, "flags_sha256": hashlib.sha256(fl.encode()).hexdigest(), "cpus": os.cpu_count()}


def binary_info(tag):
    b = os.path.join(d, "bin", f"a.{tag}")
    if not os.path.exists(b):
        return {}
    libs = {}
    for line in run("ldd", b).splitlines():
        p = line.split()
        if p and p[0].startswith(("libstdc++", "libc.so", "libc++", "libm", "libbenchmark")) and "=>" in p:
            libs[p[0]] = os.path.realpath(p[2])
    return {"comment": run("readelf", "-p", ".comment", b), "libraries": libs}


host = dict(l.split("=", 1) for l in read(os.path.join(d, "host")).splitlines() if "=" in l)
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
    "link_flags": env.get("LINK_FLAGS_USED", "-pie -fuse-ld=mold"),
    "resolved_march": run("g++-16", "-march=native", "-Q", "--help=target").split("-march=", 1)[1].split()[0],
    "compilers": [run("g++-16", "--version").splitlines()[0], run("clang++-23", "--version").splitlines()[0]],
    "gxx_binary": binary_info("gxx"),
    "clang_binary": binary_info("clang"),
    "libc": run("ldd", "--version").splitlines()[0],
    "rounds": int(env["ROUNDS"]),
    "min_time": env["MIN_TIME"],
    "cold_bytes": int(env["COLD_BYTES"]),
    "level_two_cache": host.get("level_two"),
    "build": read(os.path.join(d, "out", "build")),
    "kernel": host.get("kernel"),
    "scheduler": host.get("scheduler"),
    "virtualization": host.get("virtualization"),
    "container": "podman %s, user 65534, network none" % env["IMAGE"],
    "host_in_container": host.get("host_container"),
    "host_user": host.get("host_user"),
    "cpu": cpu(),
    "uid": os.getuid(),
    "priority": read(os.path.join(d, "out", "priority")),
    "threads": 1,
    "pinned": False,
    "cpu_left_to_the_machine": "each bench process is single-threaded on %s cpus" % host.get("cpus"),
    "gate_before_build": read(os.path.join(d, "gate-before-build")).splitlines(),
    "gate_before_runs": read(os.path.join(d, "gate-before-runs")).splitlines(),
    "steal_limit_percent": int(steal_limit),
    "rounds_detail": rounds,
    "failed": sorted(set(read(os.path.join(d, "out", "failed.txt")).split())),
    "stderr_of_runs": read(os.path.join(d, "out", "err.txt"))[:4000],
    "noise_floor": {"a_over_a_max_deviation_percent": 100 * max(aa_dev, default=0)},
    "difference_test": "two-tailed Mann-Whitney U test of compare.py of Google Benchmark over the rounds, alpha %s, and B/A further from 1 than A/A and than the noise floor of the machine (%s %%)" % (utest_alpha, env["NOISE_FLOOR_PERCENT"]),
    "compare_text": {tag: {pair: u.get("text", "") for pair, u in p.items()} for tag, p in utests.items()},
    "cells": cells,
}, indent=1))
PY
}

"$1"
