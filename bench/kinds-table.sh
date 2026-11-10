#!/bin/bash
# Measures the kinds table of README.md: carsales (bench/cars.json), one field, the whole message and a write, for
# each kind of access of mbedded-cbor (lazy, at_path, item, structs, schema, schema unverified) and beside each a
# library that does the same work (tinycbor, jsoncons, FlatBuffers). Every binary is checked against the value of
# the source data before a time is kept.
# usage: bench/kinds-table.sh, on the host. It builds and measures in podman, in the image of
# containers/debian-sid-bench.Containerfile.
# Variables: ROUNDS (5), MIN_TIME (3s), COMPILERS ("g++-16 clang++-23"), ARMS (every arm), RESULTS_DIR
# (bench/results), CBOR_BUILD_ROOT, BENCH_IMAGE.
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
field="LAZY_FIELD TC_FIELD PATH_FIELD JC_PATH_FIELD ITEM_FIELD JC_ITEM_FIELD STRUCTS_FIELD JC_STRUCT_FIELD MB_FIELD FB_FIELD_V MB_FIELD_UNVERIFIED FB_FIELD MB_FIELD_AA"
whole="LAZY_READ TC_READ PATH_READ JC_PATH_READ ITEM_READ JC_ITEM_READ STRUCTS_READ JC_STRUCT_READ MB_READ FB_READ_V MB_READ_UNVERIFIED FB_READ MB_READ_AA"
write="ITEM_ENCODE JC_ITEM_ENCODE STRUCTS_ENCODE JC_STRUCT_ENCODE MB_ENCODE FB_ENCODE MB_ENCODE_AA"
arms=${ARMS:-"$field $whole $write"}
mkdir -p "$root/kinds" "$RESULTS_DIR"
take_run_lock "$root"
stamp=$(date -u +%Y-%m-%d-%H%M%SZ)
run=$root/kinds/$stamp
mkdir -p "$run/out"
trap 'rm -rf "${run:?}"' EXIT
commit=$(name_bench_commit "$repo" include bench cmake test/owned_copy.hpp)
build_and_measure_in_container "$image" "$run" table-run.sh -v "$repo/include":/include:ro \
	-v "$repo/test/owned_copy.hpp":/test/owned_copy.hpp:ro -e ARMS="$arms" \
	-e COMPILERS="${COMPILERS:-}" -e ROUNDS="${ROUNDS:-5}" -e MIN_TIME="${MIN_TIME:-3s}"
python3 -I "$here/kinds-table.py" "$run" "$RESULTS_DIR/kinds-$stamp.json" "$commit" "$field" "$whole" "$write"
