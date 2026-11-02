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
  PYTHONDONTWRITEBYTECODE=1 PYTHONPATH=/src/tools/doxygen python3 -m unittest /src/tools/doxygen/test_free_man.py
  python3 /src/tools/doxygen/classic-man.py $work/doxygen/xml $work/classic --version "$VERSION" --date "$TODAY"
  cp $work/classic/cbor::sequence.3 $work/sequence.3
  cp $work/classic/cbor::sequence::iterator.3 $work/sequence_iterator.3
  python3 /src/tools/doxygen/free-man.py $work/doxygen/xml cbor::item_size $work/item_size.3 --version "$VERSION" --date "$TODAY" \
    --xref "sequence.md=cbor::sequence(3)" --xref "../errors/limits.md=cbor::error(3)" \
    --xref "../../advanced/basics.md=advanced/basics.md, section 5"
  python3 /src/tools/doxygen/free-man.py $work/doxygen/xml cbor::diagnostic_notation $work/diagnostic_notation.3 --version "$VERSION" --date "$TODAY" \
    --xref "item_size.md=cbor::item_size(3)" --xref "../errors/limits.md=cbor::error(3)" \
    --xref "../../advanced/basics.md=advanced/basics.md, section 5"
  body() { sed "s/^\(\.TH \"[^\"]*\" \"[^\"]*\" \)\"[^\"]*\"/\1\"\"/" "$1"; }
  for page in $work/*.3; do
    file=$(basename $page)
    committed=/src/man/man3/$file
    if [ -f $committed ] && [ "$(body $committed)" = "$(body $page)" ]; then
      [ $committed -ef /out/$file ] || cp $committed /out/$file
    else
      cp $page /out/$file
    fi
  done
  mandoc -Tlint -W warning /out/*.3'
