#!/bin/sh
# Builds the site in the given directory: the index and the page of cbor::lazy::at_path from
# doc/reference/lazy/at_path.md, with the implementation from the tokens of the headers before "See also". The tokens
# and the highlighted headers stay in the container; the checks compare them with the headers.
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
  page=/src/doc/reference/lazy/at_path.md
  mkdir -p /out/reference/lazy
  cp /src/tools/site/style.css /out/style.css
  {
    sed "/^## See also/,\$d" "$page"
    printf "## Implementation\n\n<details><summary>The declarations and definitions in the headers</summary>\n\n"
    cat /tmp/work/impl/lazy/at_path.html
    printf "\n</details>\n\n"
    sed -n "/^## See also/,\$p" "$page"
  } > /tmp/at_path.md
  page() { pandoc -s -f gfm -t html5 --template /src/tools/site/template.html "$@"; }
  page -V root=../../ -M pagetitle="cbor::lazy::at_path" /tmp/at_path.md -o /out/reference/lazy/at_path.html
  page -V root= -M pagetitle=mbedded-cbor /src/tools/site/index.md -o /out/index.html
  cp /src/tools/site/lazy.html /out/lazy.html
  clang++-23 --target=wasm32-wasip1 -mexec-model=reactor -std=c++26 -O2 -fno-exceptions -Wall -Wextra -Werror \
    -I/src/include -I/src/third_party/boost_pfr/include -Wl,--strip-all,--gc-sections -o /out/examples.wasm /src/tools/site/examples.cpp
  lynx -dump -nolist /out/reference/lazy/at_path.html > /tmp/at_path.txt
  for heading in Parameters "Return value" Errors Cost Examples Implementation "See also"; do
    grep -q "^$heading\$\|^   $heading\$" /tmp/at_path.txt || { echo "page: no section $heading in lynx"; exit 1; }
  done
  grep -q "key_not_found" /tmp/at_path.txt
  grep -q "singular_query_walk" /tmp/at_path.txt
  grep -q "href=\"$SOURCE_URL/query.hpp#L" /out/reference/lazy/at_path.html
  echo "page: reference/lazy/at_path.html, every section in lynx"'
