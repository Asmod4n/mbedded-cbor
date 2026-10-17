#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
podman run --rm -v "$tree":/src:ro,Z -v "$out":/out:Z localhost/mbedded-cbor-wasm sh -eu -c '
base="clang++-23 --target=wasm32-wasip1 -mexec-model=reactor -std=c++26 -fno-exceptions -Wall -Wextra -Werror -I/src/include /src/tools/wasm/showcase.cpp"
strip="-Wl,--strip-all,--gc-sections"
one() { name=$1; shift; $base "$@" -o /out/showcase-$name.wasm 2>/out/showcase-$name.log || echo "$name failed"; }
one O2 -O2
one O2s -O2 $strip
one O3s -O3 $strip
one Oss -Os $strip
one Ozs -Oz $strip
one O2s-lto -O2 -flto $strip
one O3s-lto -O3 -flto $strip
one O2s-simd -O2 -msimd128 $strip
one O3s-simd -O3 -msimd128 $strip
one O2s-bulk -O2 -mbulk-memory $strip
one O2s-nortti -O2 -fno-rtti $strip
wasm-opt -O3 --enable-bulk-memory --enable-sign-ext --enable-mutable-globals --enable-nontrapping-float-to-int --enable-reference-types --enable-multivalue /out/showcase-O2s.wasm -o /out/showcase-O2s-wopt3.wasm
wasm-opt -Oz --enable-bulk-memory --enable-sign-ext --enable-mutable-globals --enable-nontrapping-float-to-int --enable-reference-types --enable-multivalue /out/showcase-O2s.wasm -o /out/showcase-O2s-woptz.wasm
wasm-opt -O3 --enable-simd --enable-bulk-memory --enable-sign-ext --enable-mutable-globals --enable-nontrapping-float-to-int --enable-reference-types --enable-multivalue /out/showcase-O3s-simd.wasm -o /out/showcase-O3s-simd-wopt3.wasm
for f in /out/showcase-*.wasm; do
  printf "%s %s %s %s\n" "$(basename $f .wasm)" "$(stat -c %s $f)" "$(gzip -9c $f | wc -c)" "$(brotli -cZ $f | wc -c)"
done > /out/sizes.txt
wasm-objdump -h /out/showcase-O2.wasm > /out/sections-O2.txt
wasm-objdump -h /out/showcase-O2s.wasm > /out/sections-O2s.txt
llvm-nm-23 --size-sort -S /out/showcase-O2.wasm 2>&1 | tail -25 > /out/symbols-O2.txt || true
'
