#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
podman run --rm -v "$tree":/src:ro,Z -v "$out":/out:Z localhost/mbedded-cbor-wasm \
  clang++-23 --target=wasm32-wasip1 -mexec-model=reactor -std=c++26 -O2 -fno-exceptions -Wall -Wextra -Werror \
  -I/src/include -o /out/showcase.wasm /src/tools/wasm/showcase.cpp
