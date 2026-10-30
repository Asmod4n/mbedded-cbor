#!/bin/sh
# Builds the site in the given directory: the index, the page of cbor::lazy and the WASM module its examples run in.
# The tokens and the highlighted headers stay in the container; the checks compare them with the headers.
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
mkdir -p "$out"
commit=$(git -C "$tree" rev-parse HEAD)
podman run --rm --network none -e SOURCE_URL="https://github.com/Asmod4n/mbedded-cbor/blob/$commit/include/cbor" -v "$tree":/src:ro -v "$out":/out mbedded-cbor-site sh -ec '
  flags="-std=c++23 -O1 -Wall -Wextra -Werror -I/src/include -I/src/third_party/boost_pfr/include -I/usr/lib/llvm-23/include"
  g++-16 $flags /src/tools/site/tokens.cpp -L/usr/lib/llvm-23/lib -lclang -Wl,-rpath,/usr/lib/llvm-23/lib -o /tmp/tokens
  g++-16 $flags /src/tools/site/check.cpp -o /tmp/check
  /tmp/tokens /src /tmp/work "$SOURCE_URL"
  /tmp/check /src /tmp/work
  cp /src/tools/site/style.css /out/style.css
  page() { pandoc -s -f gfm -t html5 --template /src/tools/site/template.html "$@"; }
  page -V root= -M pagetitle=mbedded-cbor /src/tools/site/index.md -o /out/index.html
  cp /src/tools/site/lazy.html /out/lazy.html
  clang++-23 --target=wasm32-wasip1 -mexec-model=reactor -std=c++26 -O2 -fno-exceptions -Wall -Wextra -Werror \
    -I/src/include -I/src/third_party/boost_pfr/include -Wl,--strip-all,--gc-sections -o /out/examples.wasm /src/tools/site/examples.cpp
  lynx -dump -nolist /out/lazy.html > /tmp/lazy.txt
  for text in at_path size type Console "key_not_found\|42"; do
    grep -q "$text" /tmp/lazy.txt || { echo "page: no $text in lynx"; exit 1; }
  done
  echo "page: lazy.html in lynx"'
