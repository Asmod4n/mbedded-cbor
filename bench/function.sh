#!/bin/bash
# usage: bench/function.sh <commit-a> <commit-b> <block>
# Measures the functions of bench/function/<block>.cpp for two commits with g++ and clang++ in podman. See README.md.
set -eu
[ $# -eq 3 ] || { echo "usage: $0 <commit-a> <commit-b> <block>" >&2; exit 2; }
commit_a=$1
commit_b=$2
block=$3
here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "$here")
source_file=bench/function/$block.cpp
[ -f "$repo/$source_file" ] || { echo "no file $source_file" >&2; exit 2; }
root=${CBOR_BUILD_ROOT:-/home/user/mbedded-cbor-build}
cache=${CCACHE_HOST_DIR:-/home/user/.ccache}
image=${BENCH_IMAGE:-mbedded-cbor-sid-bench}
RESULTS_DIR=${RESULTS_DIR:-$here/results}
ROUNDS=${ROUNDS:-9}
MIN_TIME=${MIN_TIME:-0.05s}
level_two=$(cat /sys/devices/system/cpu/cpu0/cache/index2/size)
COLD_BYTES=${COLD_BYTES:-$((2 * ${level_two%K} * 1024))}
TIME_LIMIT=${TIME_LIMIT:-300}
[ -n "${NOISE_FLOOR_PERCENT:-}" ] || { echo "NOISE_FLOOR_PERCENT: the largest deviation of B/A from 1 with commit a as commit b on this machine" >&2; exit 2; }
GATE_SECONDS=${GATE_SECONDS:-2}

for tool in podman git flock vmstat ps python3 tar; do
	command -v "$tool" > /dev/null || { echo "missing tool: $tool" >&2; exit 2; }
done
. "$here/gate.sh"

mkdir -p "$root" "$cache" "$root/function/pch" "$RESULTS_DIR"
exec 9> "$root/run.lock"
flock -n 9 || { echo "another build or bench holds $root/run.lock" >&2; exit 2; }

stamp=$(date -u +%Y-%m-%d-%H%M%SZ)
run=$root/function/$stamp
mkdir -p "$run/src/a" "$run/src/b" "$run/out"
trap 'rm -rf "$run"' EXIT
for v in a b; do
	c=commit_$v
	git -C "$repo" archive "${!c}" include | tar -x -C "$run/src/$v"
	git -C "$repo" rev-parse --verify "${!c}^{commit}" > "$run/src/$v/id"
done
cp "$repo/$source_file" "$run/bench.cpp"
bench_commit=$(git -C "$repo" log -n 1 --format=%H -- "$source_file")
[ -n "$bench_commit" ] || bench_commit=uncommitted
[ -z "$(git -C "$repo" status --porcelain -- "$source_file" bench/function.sh bench/function-run.sh bench/gate.sh)" ] ||
	bench_commit="$bench_commit-dirty"

gate > "$run/gate-before-build" 2>&1 || { cat "$run/gate-before-build"; echo "the machine is busy; no run was made" >&2; exit 3; }
cat "$run/gate-before-build"

{
	echo "kernel=$(uname -r)"
	echo "virtualization=$(systemd-detect-virt 2>&1 | tr '\n' ' ' || true)"
	echo "host_container=$([ -e /run/.containerenv ] || [ -e /.dockerenv ] && echo yes || echo no)"
	echo "scheduler=$(cat /sys/kernel/sched_ext/state 2>&1 || echo 'no sched_ext state (kernel default)')"
	echo "host_user=$(id -un) $(id -u)"
	echo "cpus=$(nproc)"
	echo "level_two=$level_two"
} > "$run/host"

podman run --rm --network none --user 65534:65534 --cap-add SYS_NICE --timeout="$TIME_LIMIT" --init \
	-v "$run":/run-dir:U -v "$cache":/ccache:U -v "$root/function/pch":/pch:U -v "$here/function-run.sh":/function-run.sh:ro \
	-e CCACHE_DIR=/ccache -e CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime \
	-e ROUNDS="$ROUNDS" -e MIN_TIME="$MIN_TIME" -e COLD_BYTES="$COLD_BYTES" -e FLAGS="${FLAGS:-}" \
	"$image" bash /function-run.sh build

gate > "$run/gate-before-runs" 2>&1 || { cat "$run/gate-before-runs"; echo "the machine is busy after the build; no run was made" >&2; exit 3; }

podman run --rm --network none --user 65534:65534 --cap-add SYS_NICE --timeout="$TIME_LIMIT" --init \
	-v "$run":/run-dir:U -v "$here/function-run.sh":/function-run.sh:ro \
	-e ROUNDS="$ROUNDS" -e MIN_TIME="$MIN_TIME" -e COLD_BYTES="$COLD_BYTES" -e FLAGS="${FLAGS:-}" \
	-e NOISE_FLOOR_PERCENT="$NOISE_FLOOR_PERCENT" -e BLOCK="$block" -e STAMP="$stamp" -e COMMIT_A="$commit_a" -e COMMIT_B="$commit_b" -e BENCH_COMMIT="$bench_commit" \
	-e IMAGE="$image" "$image" bash /function-run.sh measure

cp "$run/out/result.json" "$RESULTS_DIR/function-$block-$stamp.json"
echo "wrote $RESULTS_DIR/function-$block-$stamp.json"
python3 -I "$here/function-table.py" "$RESULTS_DIR/function-$block-$stamp.json"
