#!/bin/bash
# Measures the table "Performance" of README.md on this machine: carsales, one field, the whole message and an
# encode, for mbedded-cbor, FlatBuffers with its Verifier and Cap'n Proto.
# usage: bench/readme-table.sh
# Needs: g++-16 with -freflection, flatc, capnp, the headers and libraries of flatbuffers, capnproto, nlohmann_json
# and google benchmark. Variables: ROUNDS (7), MIN_TIME (1s), GXX (g++-16), FLAGS, RESULTS_DIR (bench/results).
set -eu
[ "$(id -u)" = 0 ] && { echo "run it as a normal user" >&2; exit 2; }
src=$(cd "$(dirname "$0")/.." && pwd)
rounds=${ROUNDS:-7}
min_time=${MIN_TIME:-1s}
gxx=${GXX:-g++-16}
flags=${FLAGS:--O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie -DNDEBUG}
results=${RESULTS_DIR:-$src/bench/results}
jobs=$(( $(nproc) > 1 ? $(nproc) - 1 : 1 ))
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/gen" "$work/bin" "$work/out"
flatc --cpp --gen-object-api -o "$work/gen" "$src/bench/carsales.fbs"
capnp compile -oc++:"$work/gen" --src-prefix="$src/bench" "$src/bench/carsales.capnp"
arms="MB_FIELD MB_FIELD_AA FB_FIELD_V CP_FIELD MB_READ FB_READ_V CP_READ MB_ENCODE FB_ENCODE CP_ENCODE"
echo "building $(echo $arms | wc -w) binaries with $jobs jobs"
for arm in $arms; do
	op=${arm%_AA}
	"$gxx" -std=c++26 -freflection $flags -DDS_CARS -DOP_$op -DCARS_JSON="\"$src/bench/cars.json\"" \
		-DDOC_PATH="\"$src/bench/docs/cars.cbor\"" -I"$src/include" -I"$work/gen" "$src/bench/compare.cpp" \
		"$work/gen/carsales.capnp.c++" -lcapnp -lkj -lflatbuffers -lbenchmark -lpthread -o "$work/bin/$arm" &
	while [ "$(jobs -rp | wc -l)" -ge "$jobs" ]; do wait -n; done
done
wait
nice_level=0
nice -n -10 true 2>/dev/null && nice_level=-10
for r in $(seq 1 "$rounds"); do
	for arm in $(printf '%s\n' $arms | shuf); do
		nice -n "$nice_level" "$work/bin/$arm" --benchmark_min_time="$min_time" --benchmark_format=json \
			> "$work/out/$r.$arm.json"
	done
	echo "round $r of $rounds"
done
mkdir -p "$results"
file="$results/readme-$(date -u +%Y-%m-%d-%H%M%SZ).json"
commit=$(git -C "$src" rev-parse HEAD 2>/dev/null || echo unknown)
git -C "$src" diff --quiet 2>/dev/null || commit="$commit-dirty"
python3 - "$work/out" "$file" "$commit" \
	"$flags" "$("$gxx" --version | head -1)" "$(uname -srm)" "$(grep -m1 'model name' /proc/cpuinfo | cut -d: -f2)" \
	"$nice_level" "$rounds" "$min_time" <<'PY'
import glob, json, os, re, statistics, sys
out, file, commit, flags, compiler, kernel, cpu, nice, rounds, min_time = sys.argv[1:]
runs = {}
for f in glob.glob(os.path.join(out, '*.json')):
    r, arm = re.match(r'(\d+)\.(.+)\.json$', os.path.basename(f)).groups()
    b = [x for x in json.load(open(f))['benchmarks'] if x.get('run_type', 'iteration') == 'iteration'][0]
    runs.setdefault(arm, {})[int(r)] = (b['real_time'], b['time_unit'])
res = {}
for arm, v in sorted(runs.items()):
    t = [v[i][0] for i in sorted(v)]
    m = statistics.median(t)
    res[arm] = {'unit': v[1][1], 'repetitions': t, 'median': m, 'spread': max(t) / min(t) - 1}
json.dump({'what': 'README table: carsales, compare.cpp', 'commit': commit, 'flags': flags, 'compiler': compiler,
           'kernel': kernel, 'cpu': cpu.strip(), 'nice': nice, 'rounds': rounds, 'min_time': min_time,
           'results': res}, open(file, 'w'), indent=1)
def show(arm):
    r = res[arm]
    return f"{r['median']:.4g} {r['unit']} (±{r['spread'] * 100:.0f} %)"
print(f"\n{'task':<14} {'mbedded-cbor':<20} {'FlatBuffers+Verifier':<22} Cap'n Proto")
for task, a, b, c in [('one field', 'MB_FIELD', 'FB_FIELD_V', 'CP_FIELD'), ('whole message', 'MB_READ', 'FB_READ_V', 'CP_READ'),
                      ('write', 'MB_ENCODE', 'FB_ENCODE', 'CP_ENCODE')]:
    print(f"{task:<14} {show(a):<20} {show(b):<22} {show(c)}")
print(f"noise (A/A of one field): {res['MB_FIELD_AA']['median'] / res['MB_FIELD']['median'] - 1:+.1%}")
print(f"\nwritten: {file}")
PY
