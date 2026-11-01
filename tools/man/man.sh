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
  cd /src
  CBOR_DOXYGEN_OUTPUT=$work/doxygen doxygen /src/tools/doxygen/sequence.Doxyfile
  python3 /src/tools/doxygen/classic-man.py $work/doxygen/xml $work/classic --version "$VERSION" --date "$TODAY"
  cp $work/classic/cbor::sequence.3 $work/sequence.3
  cp $work/classic/cbor::sequence::iterator.3 $work/sequence_iterator.3
  cp $work/classic/cbor::item_size.3 $work/item_size.3
  body() { sed "s/^\(\.TH \"[^\"]*\" \"[^\"]*\" \)\"[^\"]*\"/\1\"\"/" "$1"; }
  for page in $work/*.3; do
    file=$(basename $page)
    committed=/src/doc/man/man3/$file
    if [ -f $committed ] && [ "$(body $committed)" = "$(body $page)" ]; then
      [ $committed -ef /out/$file ] || cp $committed /out/$file
    else
      cp $page /out/$file
    fi
  done
  mandoc -Tlint -W warning /out/*.3'
