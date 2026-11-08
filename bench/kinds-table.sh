#!/bin/bash
# Measures the kinds table of README.md: carsales (bench/cars.json), one field, the whole message and a write, for
# each kind of access of mbedded-cbor (lazy, at_path, item, structs, schema, schema unverified) and beside each a
# library that does the same work (tinycbor, jsoncons, FlatBuffers). Every binary is checked against the value of
# the source data before a time is kept.
# usage: bench/kinds-table.sh, inside the image of containers/debian-sid-bench.Containerfile, as a normal user.
# Variables: ROUNDS (5), MIN_TIME (3s), COMPILERS ("g++-16 clang++-23"), RESULTS_DIR (bench/results), WORK (a new
# temporary directory), BENCH_COMMIT (the commit when the tree is no git checkout), CONTAINER (a name for the record).
set -eu
[ "$(id -u)" = 0 ] && { echo "run it as a normal user" >&2; exit 2; }
src=$(cd "$(dirname "$0")/.." && pwd)
rounds=${ROUNDS:-5}
min_time=${MIN_TIME:-3s}
compilers=${COMPILERS:-"g++-16 clang++-23"}
results=${RESULTS_DIR:-$src/bench/results}
flags="-O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie -DNDEBUG"
jobs=$(( $(nproc) > 1 ? $(nproc) - 1 : 1 ))
if [ -n "${WORK:-}" ]; then
	work=$WORK
else
	work=$(mktemp -d)
	trap 'rm -rf "$work"' EXIT
fi
mkdir -p "$work/gen" "$work/bin" "$work/out" "$work/log"
flatc --cpp --gen-object-api -o "$work/gen" "$src/bench/carsales.fbs"
capnp compile -oc++:"$work/gen" --src-prefix="$src/bench" "$src/bench/carsales.capnp"
field="LAZY_FIELD TC_FIELD PATH_FIELD JC_PATH_FIELD ITEM_FIELD JC_ITEM_FIELD STRUCTS_FIELD JC_STRUCT_FIELD MB_FIELD FB_FIELD_V MB_FIELD_UNVERIFIED FB_FIELD MB_FIELD_AA"
whole="LAZY_READ TC_READ PATH_READ JC_PATH_READ ITEM_READ JC_ITEM_READ STRUCTS_READ JC_STRUCT_READ MB_READ FB_READ_V MB_READ_UNVERIFIED FB_READ MB_READ_AA"
write="ITEM_ENCODE JC_ITEM_ENCODE STRUCTS_ENCODE JC_STRUCT_ENCODE MB_ENCODE FB_ENCODE MB_ENCODE_AA"
arms="$field $whole $write"
for cxx in $compilers; do
	std=-std=c++23
	case $cxx in g++*) std="-std=c++26 -freflection" ;; esac
	for arm in $arms; do
		op=${arm%_AA}
		(
			"$cxx" $std $flags -DDS_CARS -DOP_$op -DCARS_JSON="\"$src/bench/cars.json\"" -I"$src/include" -isystem "$work/gen" \
				"$src/bench/compare.cpp" "$work/gen/carsales.capnp.c++" -lcapnp -lkj -lflatbuffers -ltinycbor -lbenchmark -lpthread \
				-o "$work/bin/$cxx.$arm" >"$work/log/$cxx.$arm.log" 2>&1 || { echo "build failed: $cxx $arm"; head -5 "$work/log/$cxx.$arm.log"; }
		) &
		while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do wait -n || true; done
	done
done
wait
nice_level=0
[ "$(nice -n -10 nice)" = -10 ] && nice_level=-10
binaries=$(cd "$work/bin" && ls)
for r in $(seq 1 "$rounds"); do
	for b in $(printf '%s\n' $binaries | shuf); do
		nice -n "$nice_level" "$work/bin/$b" --benchmark_min_time="$min_time" --benchmark_format=json >"$work/out/$r.$b.json"
	done
	echo "round $r of $rounds"
done
mkdir -p "$results"
file="$results/kinds-$(date -u +%Y-%m-%d-%H%M%SZ).json"
if commit=$(git -C "$src" rev-parse HEAD 2>&1); then
	git -C "$src" diff --quiet || commit="$commit-dirty"
else
	commit=${BENCH_COMMIT:-unknown}
fi
any=$(printf '%s\n' $binaries | head -1)
entry() { printf '%s=%s\n' "$1" "$(printf '%s' "$2" | tr '\n' ' ')"; }
context=$(
	printf 'commit=%s\n' "$commit"
	printf 'flags=%s\n' "$flags"
	printf 'march=%s\n' "$(g++-16 -march=native -Q --help=target | sed -n 's/^ *-march= *//p' | head -1)"
	for cxx in $compilers; do printf 'compiler.%s=%s\n' "$cxx" "$("$cxx" --version | head -1)"; done
	printf 'cxx_library=%s\n' "$(ldd "$work/bin/$any" | grep -o 'libstdc++[^ ]*\|libc++[^ ]*' | head -1) $(dpkg-query -W -f '${Version}' libstdc++6)"
	printf 'c_library=%s\n' "$(ldd --version | head -1)"
	printf 'kernel=%s\n' "$(uname -srvm)"
	entry scheduler "$(cat /sys/kernel/debug/sched/features 2>&1 | head -c 200)"
	entry virtualization "$(grep -m1 -o hypervisor /proc/cpuinfo || echo none) $(cat /sys/class/dmi/id/sys_vendor /sys/class/dmi/id/product_name 2>&1)"
	printf 'container=%s\n' "${CONTAINER:-podman}"
	printf 'cpu=%s\n' "$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2 | sed 's/^ //')"
	printf 'cpu_flags_sha256=%s\n' "$(grep -m1 '^flags' /proc/cpuinfo | sha256sum | cut -d' ' -f1)"
	printf 'cpus=%s\n' "$(nproc)"
	entry user "$(id -u)"
	printf 'nice=%s\n' "$nice_level"
	printf 'threads=1\n'
	printf 'rounds=%s\n' "$rounds"
	printf 'min_time=%s\n' "$min_time"
	printf 'libraries=%s\n' "$(dpkg-query -W -f '${Package} ${Version}, ' libtinycbor-dev libjsoncons-dev libflatbuffers-dev libbenchmark-dev)"
)
python3 - "$work/out" "$file" "$field" "$whole" "$write" <<PY
import glob, json, os, re, statistics, sys
out, file, field, whole, write = sys.argv[1:]
context = dict(line.split('=', 1) for line in """$context""".splitlines())
runs, checks = {}, {}
for f in glob.glob(os.path.join(out, '*.json')):
    r, b = re.match(r'(\d+)\.(.+)\.json$', os.path.basename(f)).groups()
    d = json.load(open(f))
    x = [y for y in d['benchmarks'] if y.get('run_type', 'iteration') == 'iteration'][0]
    runs.setdefault(b, {})[int(r)] = (x['real_time'], x['time_unit'])
    checks[b] = d['context']
bad = []
res = {}
for b, v in sorted(runs.items()):
    c = checks[b]
    arm = b.split('.', 1)[1]
    want = float(c['check_field']) if arm in field.split() else float(c['check_source'])
    got = float(c['check'])
    if abs(got - want) > 1e-9 * abs(want):
        bad.append(f"{b}: check {got!r}, expected {want!r}")
    t = [v[i][0] for i in sorted(v)]
    res[b] = {'unit': v[min(v)][1], 'repetitions': t, 'median': statistics.median(t), 'spread': max(t) / min(t) - 1,
              'check': got, 'expected': want, 'bytes_written': int(c.get('bytes_written', '0')),
              'bytes_plain': int(c['bytes_plain']), 'bytes_packed': int(c['bytes_mb'])}
json.dump({'what': 'kinds table: carsales (bench/cars.json), bench/compare.cpp, bench/kinds-table.sh', 'context': context,
           'results': res, 'check_failures': bad}, open(file, 'w'), indent=1)
print(f"written: {file}")
if bad:
    print("\n".join(bad))
    sys.exit(1)
scale = {'ns': 1, 'us': 1e3, 'ms': 1e6, 's': 1e9}
def ns(b): return res[b]['median'] * scale[res[b]['unit']]
for cxx in sorted({b.split('.', 1)[0] for b in res}):
    print(f"\n{cxx}")
    for task, arms in [('field', field), ('whole', whole), ('write', write)]:
        a = arms.split()
        aa = [x for x in a if x.endswith('_AA')][0]
        if f"{cxx}.{aa}" in res:
            print(f"  {task}: noise (A/A) {ns(f'{cxx}.{aa}') / ns(f'{cxx}.{aa[:-3]}') - 1:+.1%}")
        for x in a:
            if f"{cxx}.{x}" in res:
                print(f"    {x:<22} {ns(f'{cxx}.{x}'):>14.0f} ns ±{res[f'{cxx}.{x}']['spread'] * 100:.0f} %  bytes_written {res[f'{cxx}.{x}']['bytes_written']}")
PY
