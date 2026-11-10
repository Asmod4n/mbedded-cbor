#!/bin/bash
# Measures the table "Performance" of README.md on this machine: carsales, one field, the whole message and an
# encode, for mbedded-cbor, FlatBuffers with its Verifier and Cap'n Proto.
# usage: bench/readme-table.sh, on the host. It builds and measures in podman, in the image of
# containers/debian-sid-bench.Containerfile.
# Variables: ROUNDS (7), MIN_TIME (1s), GXX (g++-16), RESULTS_DIR (bench/results), CBOR_BUILD_ROOT, BENCH_IMAGE.
set -eu
here=$(cd "$(dirname "$0")" && pwd)
repo=$(dirname "$here")
root=${CBOR_BUILD_ROOT:-/home/user/mbedded-cbor-build}
image=${BENCH_IMAGE:-mbedded-cbor-sid-bench}
RESULTS_DIR=${RESULTS_DIR:-$here/results}
for tool in podman git flock vmstat ps python3; do
	command -v "$tool" > /dev/null || { echo "missing tool: $tool" >&2; exit 2; }
done
. "$here/gate.sh"
. "$here/host.sh"
arms="MB_FIELD MB_FIELD_AA FB_FIELD_V CP_FIELD MB_READ FB_READ_V CP_READ MB_ENCODE FB_ENCODE CP_ENCODE"
mkdir -p "$root/readme" "$RESULTS_DIR"
take_run_lock "$root"
stamp=$(date -u +%Y-%m-%d-%H%M%SZ)
run=$root/readme/$stamp
mkdir -p "$run/out"
trap 'rm -rf "${run:?}"' EXIT
commit=$(name_bench_commit "$repo" include bench cmake test/owned_copy.hpp)
build_and_measure_in_container "$image" "$run" table-run.sh -v "$repo/include":/include:ro \
	-v "$repo/test/owned_copy.hpp":/test/owned_copy.hpp:ro -e ARMS="$arms" \
	-e COMPILERS="${GXX:-g++-16}" -e ROUNDS="${ROUNDS:-7}" -e MIN_TIME="${MIN_TIME:-1s}"
python3 -I "$here/readme-table.py" "$run" "$RESULTS_DIR/readme-$stamp.json" "$commit"
