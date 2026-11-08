#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
mkdir -p "$out"
out=$(cd "$out" && pwd)
podman run --rm --network none -v "$tree":/src:ro -v "$out":/out mbedded-cbor-doxygen sh -ec '
  echo "doxygen $(doxygen --version)"
  xsltproc --version | head -1
  xml=/tmp/doxygen/xml
  cd /src
  { cat Doxyfile; echo "OUTPUT_DIRECTORY = /tmp/doxygen"; } | doxygen -
  xsltproc tools/doc/pages.xsl $xml/index.xml | sort -u > /tmp/names.txt
  while read -r name; do
    base=${name%%<*}
    base=${base% }
    file=${base##*::}
    groups=$(grep -l "]($file\.md)" doc/reference/*/index.md || true)
    if [ $(echo "$groups" | grep -c .) -ne 1 ]; then
      echo "reference: no group index, or more than one, links $file.md" >&2
      exit 1
    fi
    group=$(basename $(dirname $groups))
    if [ -e doc/reference/$group/$file.md ]; then
      echo "reference: doc/reference/$group/$file.md and the comments of $name both give the page" >&2
      exit 1
    fi
    mkdir -p /out/$group
    xsltproc --stringparam name "$name" tools/doc/reference.xsl $xml/index.xml > /out/$group/$file.md
  done < /tmp/names.txt'
