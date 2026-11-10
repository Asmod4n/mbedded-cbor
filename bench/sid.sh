#!/bin/bash
# usage: bench/sid.sh
# Measures the arms of bench/runtime.cpp against the CBOR libraries of Debian sid, with g++ and clang++ in podman.
# See README.md.
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

mkdir -p "$root/sid" "$RESULTS_DIR"
take_run_lock "$root"
stamp=$(date -u +%Y-%m-%d-%H%M%SZ)
run=$root/sid/$stamp
mkdir -p "$run/out"
trap 'rm -rf "${run:?}"' EXIT
name_bench_commit "$repo" include bench cmake test/owned_copy.hpp > "$run/commit"

build_and_measure_in_container "$image" "$run" run-sid.sh -v "$repo/include":/include:ro \
	-v "$repo/test/owned_copy.hpp":/test/owned_copy.hpp:ro -e ARMS="${ARMS:-}" -e DOCS="${DOCS:-}" \
	-e COMPILERS="${COMPILERS:-}" -e AA="${AA:-}" -e PROCESSES="${PROCESSES:-}" -e MIN_TIME="${MIN_TIME:-}" -e STAMP="$stamp"

cp "$run/out/result.json" "$RESULTS_DIR/sid-$stamp.json"
echo "wrote $RESULTS_DIR/sid-$stamp.json"
