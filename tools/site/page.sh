#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
if [ -n "$(git -C "$tree" status --porcelain)" ] && [ "${SITE_DIRTY:-}" != 1 ]; then
  echo "page: the tree has changes that are not committed; the line links would name other lines" >&2
  exit 1
fi
mkdir -p "$out"
commit=$(git -C "$tree" rev-parse HEAD)
podman run --rm --network none -e COMMIT="$commit" -e SITE_GROUPS="${SITE_GROUPS:-}" -v "$tree":/src:ro -v "$out":/out mbedded-cbor-site sh -ec '
  site=/src/tools/site
  work=/tmp/work
  mkdir -p $work
  flags="-std=c++23 -O1 -Wall -Wextra -Werror -I/src/include -I/src/third_party/boost_pfr/include -I/usr/lib/llvm-23/include"
  g++-16 $flags $site/tokens.cpp -L/usr/lib/llvm-23/lib -lclang -Wl,-rpath,/usr/lib/llvm-23/lib -o /tmp/tokens
  g++-16 $flags $site/check.cpp -o /tmp/check
  groups=$(sed -n "s/^| \[\([a-z]*\)\](\1\/index.md) |.*/\1/p" /src/doc/reference/index.md)
  for g in $groups; do
    for page in /src/doc/reference/$g/*.md; do
      file=$(basename $page .md)
      [ $file = index ] && continue
      sed -n "1s/^# //p" $page | awk -v page="$g/$file" "{
        n = split(\$0, names, \", \")
        prefix = names[1]
        sub(/::[^:]*$/, \"\", prefix)
        if (prefix == names[1]) prefix = \"\"
        line = page
        for (i = 1; i <= n; i++) line = line \"\t\" (i > 1 && prefix != \"\" ? prefix \"::\" names[i] : names[i])
        print line
      }"
    done
  done > $work/names.txt
  /tmp/tokens /src $work "https://github.com/Asmod4n/mbedded-cbor/blob/$COMMIT/include/cbor" $work/names.txt \
    test/doc_types.hpp "https://github.com/Asmod4n/mbedded-cbor/blob/$COMMIT/test/doc_types.hpp" > $work/tokens.txt
  grep -v "^missing " $work/tokens.txt
  grep "^missing " $work/tokens.txt | cut -d" " -f2 | sort > $work/missing.txt
  if ! sort $site/concepts.txt | diff - $work/missing.txt; then
    echo "page: the pages above with > have no implementation and are not in concepts.txt" >&2
    exit 1
  fi
  /tmp/check /src $work
  clang++-23 --target=wasm32-wasip1 -mexec-model=reactor -std=c++26 -O2 -fno-exceptions -Wall -Wextra -Werror \
    -I/src/include -I/src/third_party/boost_pfr/include -Wl,--strip-all,--gc-sections -o /out/examples.wasm $site/examples.cpp
  cp $site/style.css $site/site.js $site/examples.mjs /out/
  reference() {
    pandoc -f gfm -t html5 --template $site/reference.html --lua-filter $site/reference.lua \
      -M tree=/src -M impl=$work/impl -M types=$work/types -M commit=$COMMIT -M group=$1 -M examples=$work/examples-$1.json "$2" -o /out/$1.html
  }
  reference index /src/doc/reference/index.md
  for g in ${SITE_GROUPS:-$groups}; do
    reference $g /src/doc/reference/$g/index.md
    lynx -dump -nolist /out/$g.html > $work/$g.txt
  done
  node $site/check_examples.mjs /out/examples.wasm $work /src/include/cbor/error.hpp'
