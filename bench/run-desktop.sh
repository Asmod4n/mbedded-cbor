#!/bin/bash
# usage: run-desktop.sh <commit-a> <commit-b>
# Measures READ, the lazy path (MB_PATH) and at_path (MB_AT_PATH) on twitter, floats, records and tag29
# for two commits, with g++ and clang++. See README.md.
set -eu
[ $# -eq 2 ] || { echo "usage: $0 <commit-a> <commit-b>" >&2; exit 2; }
[ "$(id -u)" != 0 ] || { echo "never run as root" >&2; exit 2; }
commit_a=$1
commit_b=$2
here=$(cd "$(dirname "$0")" && pwd)
repo=${REPO:-$(dirname "$here")}
bench=$repo/bench
GXX=${GXX:-$(command -v g++-16 || command -v g++)}
CLANGXX=${CLANGXX:-$(command -v clang++-23 || command -v clang++)}
ROUNDS=${ROUNDS:-10}
MIN_TIME=${MIN_TIME:-0.2s}
docs=${DOCS:-"twitter floats records tag29"}
arms=${ARMS:-"READ MB_PATH MB_AT_PATH"}
GATE_MAX_BUSY_PERCENT=${GATE_MAX_BUSY_PERCENT:-10}
GATE_MAX_STEAL_PERCENT=${GATE_MAX_STEAL_PERCENT:-0}
RESULTS_DIR=${RESULTS_DIR:-$bench/results}
cpus=$(nproc)
jobs=${JOBS:-$((cpus > 1 ? cpus - 1 : 1))}
flags=${FLAGS:--O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie -DNDEBUG}
export GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824:glibc.malloc.mmap_threshold=33554432:glibc.malloc.top_pad=268435456

for tool in vmstat ps nice readelf ldd python3 tar; do
	command -v "$tool" > /dev/null || { echo "missing tool: $tool" >&2; exit 2; }
done

# Gate: what runs now, from vmstat 1 5 and ps --sort=-pcpu. The load average is not read.
gate() {
	local vm ps_out
	vm=$(vmstat 1 5)
	ps_out=$(ps -eo pid,user,pcpu,ni,comm --sort=-pcpu | head -n 8)
	printf 'gate: vmstat 1 5\n%s\ngate: ps --sort=-pcpu (top 7)\n%s\n' "$vm" "$ps_out"
	python3 -I -c '
import sys
vm = sys.argv[1].splitlines()
head = vm[1].split()
rows = [list(map(int, r.split())) for r in vm[3:]]
col = lambda n: sum(r[head.index(n)] for r in rows) / len(rows)
busy = 100 - col("id")
steal = col("st") if "st" in head else 0.0
ok = busy <= float(sys.argv[2]) and steal <= float(sys.argv[4])
print("gate: busy %.1f%% of all cpus (limit %s%%), steal %.1f%%, iowait %.1f%%: %s" % (busy, sys.argv[2], steal, col("wa"), "idle enough" if ok else "NOT idle enough"))
for line in sys.argv[3].splitlines()[1:]:
    f = line.split()
    if float(f[2]) >= 5.0 and f[4] not in ("ps", "head", "vmstat", "run-desktop.sh"):
        print("gate: busy process %s user %s pcpu %s ni %s %s" % (f[0], f[1], f[2], f[3], " ".join(f[4:])))
sys.exit(0 if ok else 3)
' "$vm" "$GATE_MAX_BUSY_PERCENT" "$ps_out" "$GATE_MAX_STEAL_PERCENT"
}
gate_out=$(mktemp)
if ! gate > "$gate_out" 2>&1; then
	cat "$gate_out"
	rm -f "$gate_out"
	echo "the machine is busy; no run was made and no file was written" >&2
	exit 3
fi
cat "$gate_out"

build=$(mktemp -d "${TMPDIR:-/tmp}/mbedded-cbor-desktop.XXXXXX")
trap 'rm -rf "$build"; rm -f "$gate_out"' EXIT
mkdir -p "$build/bin" "$build/out" "$build/log" "$build/src" "$build/docs"
echo "building into $build"

# The tree of a commit: include/ only. COMMIT_TREES=<dir> holds <commit>/include and <commit>.id where git is absent.
tree_of() {
	local c=$1 dest=$2
	mkdir -p "$dest"
	if [ -n "${COMMIT_TREES:-}" ]; then
		cp -r "$COMMIT_TREES/$c/include" "$dest/include"
		cat "$COMMIT_TREES/$c.id" > "$dest/id"
	else
		git -C "$repo" archive "$c" include | tar -x -C "$dest"
		git -C "$repo" rev-parse --verify "$c^{commit}" > "$dest/id"
	fi
}
tree_of "$commit_a" "$build/src/a"
tree_of "$commit_b" "$build/src/b"
cp -r "$build/src/a" "$build/src/aa"

# Documents: the repo ones, and the tag-29 input: an array of 2000, 50 x tag 28 then 1950 x tag 29.
for d in $docs; do
	if [ "$d" = tag29 ]; then
		python3 -I -c '
import sys
out = bytearray(b"\x99\x07\xd0")
for i in range(50):
    text = ("shared value %02d " % i).encode() + b"x" * 20
    out += b"\xd8\x1c\x78" + bytes([len(text)]) + text
for i in range(1950):
    n = i % 50
    out += b"\xd8\x1d" + (bytes([n]) if n < 24 else b"\x18" + bytes([n]))
open(sys.argv[1], "wb").write(out)
' "$build/docs/tag29.cbor"
	else
		cp "$bench/docs/$d.cbor" "$build/docs/$d.cbor"
	fi
done

# runtime.cpp of the repo, with the tag-29 branch added to the two path functions.
python3 -I -c '
import re, sys
src = open(sys.argv[1]).read()
branch = {
    "mb_path": """#elif defined(DOC_tag29)
    auto const v = l.and_then([](cbor::lazy const &d) { return d.at(1999); })
                       .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(**v);
""",
    "mb_at_path": """#elif defined(DOC_tag29)
    auto const v = cbor::at_path<"$[1999]", std::string_view>(keep, in);
    if (!v) [[unlikely]] std::abort();
    return bytes_sum(**v);
""",
}
for name, text in branch.items():
    start = src.index("static std::uint64_t %s()" % name)
    end = src.index("\n}\n", start)
    body = src[start:end]
    last = body.rindex("#endif")
    src = src[:start] + body[:last] + text + body[last:] + src[end:]
open(sys.argv[2], "w").write(src)
' "$bench/runtime.cpp" "$build/runtime.cpp"

compile() {
	local cc=$1 variant=$2 tag=$3 doc=$4 arm=$5
	local name=$variant.$tag.$doc.$arm
	if "$cc" -std=c++23 $flags -DDOCTEST_CONFIG_DISABLE -DARM_"$arm" -DARM_NAME="\"$arm\"" -DDOC_"$doc" \
		-DDOC_PATH="\"$build/docs/$doc.cbor\"" -I"$build/src/$variant/include" -I"$bench" \
		"$build/runtime.cpp" -lbenchmark -lpthread -o "$build/bin/$name" > "$build/log/$name" 2>&1; then
		rm -f "$build/log/$name"
	else
		rm -f "$build/bin/$name"
	fi
}
running=0
spawn() {
	if [ "$running" -ge "$jobs" ]; then
		wait -n
		running=$((running - 1))
	fi
	"$@" &
	running=$((running + 1))
}
for cc_tag in gxx:"$GXX" clang:"$CLANGXX"; do
	tag=${cc_tag%%:*}
	cc=${cc_tag#*:}
	for variant in a aa b; do
		for doc in $docs; do
			for arm in $arms; do
				spawn compile "$cc" "$variant" "$tag" "$doc" "$arm"
			done
		done
	done
done
while [ "$running" -gt 0 ]; do
	wait -n
	running=$((running - 1))
done
built=$(ls "$build/bin" | wc -l)
echo "built $built binaries; $(ls "$build/log" | wc -l) did not compile"

# Priority: nice -10 where the system allows it.
if [ "$(nice -n -10 nice 2> /dev/null)" = -10 ]; then
	runner="nice -n -10"
	priority="nice -10 granted"
else
	runner=""
	priority="nice 0 (nice -10 refused)"
fi
echo "priority: $priority"

for b in "$build"/bin/*; do
	[ -e "$b" ] || continue
	echo "$(basename "$b") $(readelf -h "$b" | grep -o 'Type: *[A-Z]*' | tr -s ' ')"
done > "$build/out/elf_type"

steal() {
	local user nice system idle iowait irq softirq stolen rest
	read -r _ user nice system idle iowait irq softirq stolen rest < /proc/stat
	echo "$stolen $((user + nice + system + idle + iowait + irq + softirq + stolen))"
}

# Cells: tag.doc.arm. Per round, the variants run one after the other in a rotating order.
cells=$(cd "$build/bin" && ls | sed -e 's/^[^.]*\.//' | sort -u)
variants=(a b aa)
start=$(date +%s)
i=1
while [ "$i" -le "$ROUNDS" ]; do
	s0=$(steal)
	t0=$(date +%s.%N)
	for cell in $cells; do
		k=0
		while [ "$k" -lt 3 ]; do
			v=${variants[$(((k + i) % 3))]}
			b=$build/bin/$v.$cell
			if [ -e "$b" ]; then
				$runner "$b" --benchmark_min_time="$MIN_TIME" --benchmark_out_format=json \
					--benchmark_out="$build/out/$i.$v.$cell.json" < /dev/null > /dev/null 2>> "$build/out/err.txt" ||
					echo "$v.$cell" >> "$build/out/failed.txt"
			fi
			k=$((k + 1))
		done
	done
	s1=$(steal)
	t1=$(date +%s.%N)
	echo "$i $t0 $t1 $s0 $s1" >> "$build/out/rounds"
	echo "round $i/$ROUNDS done, $(($(date +%s) - start)) s elapsed, steal ticks $(( ${s1% *} - ${s0% *} ))"
	i=$((i + 1))
done

mkdir -p "$RESULTS_DIR"
stamp=$(date -u +%Y-%m-%d-%H%M%SZ)
dirty=$(git -C "$repo" status --porcelain -- bench 2> /dev/null | head -n 1 || true)
bench_commit=$(git -C "$repo" rev-parse HEAD 2> /dev/null || echo unknown)
[ -z "$dirty" ] || bench_commit="$bench_commit-dirty"
python3 -I - "$build" "$stamp" "$commit_a" "$commit_b" "$bench_commit" "$flags" "$ROUNDS" "$MIN_TIME" "$GXX" "$CLANGXX" \
	"$priority" "$cpus" "$jobs" "$gate_out" "$bench" > "$RESULTS_DIR/$stamp.json.part" <<'PY'
import collections, glob, hashlib, json, os, platform, statistics, subprocess, sys

build, stamp, commit_a, commit_b, bench_commit, flags, rounds, min_time, gxx, clangxx, priority, cpus, jobs, gate_file, bench = sys.argv[1:16]


def run(*cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError:
        return ""
    return (r.stdout + r.stderr).strip()


def read(path):
    return open(path).read().strip() if os.path.exists(path) else ""


def ids(v):
    return read(os.path.join(build, "src", v, "id"))


times = collections.defaultdict(list)
context = {}
for path in sorted(glob.glob(os.path.join(build, "out", "*.json"))):
    _, v, cell = os.path.basename(path)[:-5].split(".", 2)
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

steal = []
for line in read(os.path.join(build, "out", "rounds")).splitlines():
    i, t0, t1, st0, tot0, st1, tot1 = line.split()
    steal.append({"round": int(i), "wall_s": float(t1) - float(t0), "steal_ticks": int(st1) - int(st0),
                  "steal_percent_of_cpu_time": 100.0 * (int(st1) - int(st0)) / max(1, int(tot1) - int(tot0))})


def cpu():
    model, fl = "", ""
    for line in open("/proc/cpuinfo"):
        if line.startswith("model name") and not model:
            model = line.split(":", 1)[1].strip()
        if line.startswith("flags") and not fl:
            fl = line.split(":", 1)[1].strip()
    return {"model": model, "flags_sha256": hashlib.sha256(fl.encode()).hexdigest(), "cpus": os.cpu_count(),
            "l3": read("/sys/devices/system/cpu/cpu0/cache/index3/size")}


def binary_info(tag):
    b = sorted(glob.glob(os.path.join(build, "bin", f"a.{tag}.*")))
    if not b:
        return {}
    ldd = run("ldd", b[0])
    libs = {}
    for line in ldd.splitlines():
        parts = line.split()
        if parts and (parts[0].startswith(("libstdc++", "libc.so", "libc++", "libm"))) and "=>" in parts:
            libs[parts[0]] = os.path.realpath(parts[2])
    return {"binary": os.path.basename(b[0]), "comment": run("readelf", "-p", ".comment", b[0]), "libraries": libs}


failed = sorted(set(read(os.path.join(build, "out", "failed.txt")).split()))
dirs = read("/proc/1/cgroup")
print(json.dumps({
    "time": stamp,
    "commit_a": {"given": commit_a, "id": ids("a")},
    "commit_b": {"given": commit_b, "id": ids("b")},
    "bench_sources_commit": bench_commit,
    "what": "READ, lazy path (MB_PATH) and at_path (MB_AT_PATH); a = commit a, aa = commit a built again, b = commit b",
    "documents": "twitter, floats, records, tag29 (array of 2000: 50 x tag 28, then 1950 x tag 29)",
    "flags": flags,
    "pie": "-fPIE -pie" if {"-fPIE", "-pie"} <= set(flags.split()) else "default of the compiler",
    "elf_type_count": dict(collections.Counter(l.split(" ", 1)[1] for l in read(os.path.join(build, "out", "elf_type")).splitlines())),
    "rounds": int(rounds),
    "min_time": min_time,
    "resolved_march": run(gxx, "-march=native", "-Q", "--help=target").split("-march=", 1)[1].split()[0],
    "compilers": [run(gxx, "--version").splitlines()[0], run(clangxx, "--version").splitlines()[0]],
    "gxx_binary": binary_info("gxx"),
    "clang_binary": binary_info("clang"),
    "libc": run("ldd", "--version").splitlines()[0],
    "kernel": platform.release(),
    "scheduler": read("/sys/kernel/sched_ext/state") or "no sched_ext state (kernel default)",
    "virtualization": run("systemd-detect-virt") or "unknown",
    "container": "yes" if os.path.exists("/run/.containerenv") or os.path.exists("/.dockerenv") else "no",
    "cpu": cpu(),
    "uid": os.getuid(),
    "priority": priority,
    "threads": 1,
    "pinned": False,
    "cpus_in_machine": int(cpus),
    "cpu_left_to_the_machine": "each bench process is single-threaded; builds used %s of %s cpus" % (jobs, cpus),
    "gate": read(gate_file).splitlines(),
    "noise_floor": {"a_over_a_max_deviation_percent": 100 * max(aa_dev, default=0)},
    "rounds_detail": steal,
    "steal_percent_of_cpu_time_max": max((r["steal_percent_of_cpu_time"] for r in steal), default=None),
    "failed": failed,
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
