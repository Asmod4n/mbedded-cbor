#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
mkdir -p "$out"
out=$(cd "$out" && pwd)
version=$(sed -n 's/^project(mbedded-cbor VERSION \([0-9.]*\) .*/\1/p' "$tree/CMakeLists.txt")
today=$(date -u +%F)
podman run --rm --network none -e VERSION="$version" -e TODAY="$today" -v "$tree":/src:ro -v "$out":/out mbedded-cbor-man sh -ec '
  pandoc --version | head -1
  work=/tmp/man
  mkdir -p $work
  pandoc lua /src/tools/man/man.lua /src $work "$VERSION" "$TODAY"
  body() { sed "s/^\(\.TH \"[^\"]*\" \"[^\"]*\" \)\"[^\"]*\"/\1\"\"/" "$1"; }
  for page in $work/*.3; do
    file=$(basename $page)
    committed=/src/doc/man/man3/$file
    if [ -f $committed ] && [ "$(body $committed)" = "$(body $page)" ]; then
      cp $committed /out/$file
    else
      cp $page /out/$file
    fi
  done
  mandoc -Tlint -W warning /out/*.3'
