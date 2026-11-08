#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
mkdir -p "$out"
out=$(cd "$out" && pwd)
version=$(sed -n 's/^project(mbedded-cbor VERSION \([0-9.]*\) .*/\1/p' "$tree/CMakeLists.txt")
today=$(date -u +%F)
generated=$(mktemp -d)
trap 'rm -rf "$generated"' EXIT
sh "$tree/tools/doc/reference.sh" "$generated"
podman run --rm --network none -e VERSION="$version" -e TODAY="$today" -v "$tree":/src:ro -v "$generated":/generated:ro -v "$out":/out mbedded-cbor-docs sh -ec '
  pandoc --version | head -1
  work=/tmp/man
  mkdir -p $work/reference
  cp -r /src/tools/site/reference/. /generated/. $work/reference
  pandoc lua /src/tools/man/man.lua $work/reference $work "$VERSION" "$TODAY"
  body() { sed "s/^\(\.TH \"[^\"]*\" \"[^\"]*\" \)\"[^\"]*\"/\1\"\"/" "$1"; }
  guides=/src/tools/site/guides
  mkdir -p $work/man7
  for guide in $guides/*.md $guides/advanced/*.md; do
    topic=$(basename $guide .md)
    case $guide in $guides/advanced/*) topic=advanced-$topic ;; esac
    pandoc -s -f gfm -t man --lua-filter /src/tools/man/guide.lua -M title=mbedded-cbor-$topic -M section=7 \
      -M footer="mbedded-cbor $VERSION" -M header=mbedded-cbor "$guide" -o $work/man7/page
    sed "/^\.S[HS] /{n;/^\.PP\$/d;}" $work/man7/page > $work/man7/mbedded-cbor-$topic.7
    rm $work/man7/page
  done
  mkdir -p /out/man3 /out/man7
  kept() {
    page=$1 committed=$2 target=$3
    if [ -f $committed ] && [ "$(body $committed)" = "$(body $page)" ]; then
      [ $committed -ef $target ] || cp $committed $target
    else
      cp $page $target
    fi
  }
  for page in $work/*.3; do kept $page /src/man/man3/$(basename $page) /out/man3/$(basename $page); done
  for page in $work/man7/*.7; do kept $page /src/man/man7/$(basename $page) /out/man7/$(basename $page); done
  mandoc -Tlint -W warning /out/man3/*.3 /out/man7/*.7'
