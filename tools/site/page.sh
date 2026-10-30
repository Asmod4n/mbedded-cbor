#!/bin/sh
# Builds the sample page of cbor::lazy::at_path in the given directory: the tokens and the highlighted headers,
# then the page from doc/reference/lazy/at_path.md with the implementation before "See also", then the checks.
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
mkdir -p "$out"
podman run --rm --network none -v "$tree":/src:ro -v "$out":/out mbedded-cbor-site sh -ec '
  flags="-std=c++23 -O1 -Wall -Wextra -Werror -I/src/include -I/src/third_party/boost_pfr/include -I/usr/lib/llvm-23/include"
  g++-16 $flags /src/tools/site/tokens.cpp -L/usr/lib/llvm-23/lib -lclang -Wl,-rpath,/usr/lib/llvm-23/lib -o /tmp/tokens
  g++-16 $flags /src/tools/site/check.cpp -o /tmp/check
  /tmp/tokens /src /out
  /tmp/check /src /out
  page=/src/doc/reference/lazy/at_path.md
  mkdir -p /out/reference/lazy
  cp /src/tools/site/style.css /out/style.css
  {
    sed "/^## See also/,\$d" "$page"
    printf "## Implementation\n\n<details><summary>The declarations and definitions in the headers</summary>\n\n"
    cat /out/impl/lazy/at_path.html
    printf "\n</details>\n\n"
    sed -n "/^## See also/,\$p" "$page"
  } > /tmp/at_path.md
  pandoc -s -f gfm -t html5 --css ../../style.css -M document-css=false -M pagetitle="cbor::lazy::at_path" \
    /tmp/at_path.md -o /out/reference/lazy/at_path.html
  lynx -dump -nolist /out/reference/lazy/at_path.html > /out/reference/lazy/at_path.txt
  for heading in Parameters "Return value" Errors Cost Examples Implementation "See also"; do
    grep -q "^$heading\$\|^   $heading\$" /out/reference/lazy/at_path.txt || { echo "page: no section $heading in lynx"; exit 1; }
  done
  grep -q "key_not_found" /out/reference/lazy/at_path.txt
  grep -q "singular_query_walk" /out/reference/lazy/at_path.txt
  echo "page: reference/lazy/at_path.html, every section in lynx"'
