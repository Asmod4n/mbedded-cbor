#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
mkdir -p "$out"
out=$(cd "$out" && pwd)
cd "$tree"
CBOR_DOXYGEN_OUTPUT="$out" doxygen tools/doxygen/sequence.Doxyfile
python3 tools/doxygen/fix-navigation.py "$out/html"
python3 tools/doxygen/classic-man.py "$out/xml" "$out/classic-man/man3"
