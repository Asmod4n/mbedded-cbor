#!/bin/sh
# Builds tools/wasm/showcase.wasm and its source map showcase.wasm.map in the given directory. The clang driver
# runs wasm-opt without -g, which drops the line table, so the build runs wasm-opt itself with -g, makes the map
# from the line table, and then strips every custom section from the wasm that a page loads. A page loads the map
# itself, only when it runs an example.
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
podman run --rm -v "$tree":/src:ro,Z -v "$out":/out:Z localhost/mbedded-cbor-wasm sh -ec '
  clang++-23 --target=wasm32-wasip1 -mexec-model=reactor -std=c++26 -O2 -flto -g --no-wasm-opt -fno-exceptions \
    -Wall -Wextra -Werror -I/src/include -Wl,--gc-sections -o /out/showcase.debug.wasm /src/tools/wasm/showcase.cpp
  wasm-opt -O2 -g /out/showcase.debug.wasm -o /out/showcase.debug.wasm
  python3 /usr/share/emscripten/tools/wasm-sourcemap.py /out/showcase.debug.wasm --dwarfdump=/usr/bin/llvm-dwarfdump-23 \
    -o /out/showcase.wasm.map -w /out/showcase.mapped.wasm --prefix /src=
  wasm-strip /out/showcase.mapped.wasm -o /out/showcase.wasm
  rm /out/showcase.debug.wasm /out/showcase.mapped.wasm'
