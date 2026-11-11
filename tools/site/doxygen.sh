#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
mkdir -p "$out"
out=$(cd "$out" && pwd)
podman run --rm --network none -v "$tree":/src:ro -v "$out":/out mbedded-cbor-docs sh -ec '
  cd /src
  doxygen tools/site/Doxyfile'
