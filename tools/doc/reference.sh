#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
mkdir -p "$out"
out=$(cd "$out" && pwd)
podman run --rm --network none -v "$tree":/src:ro -v "$out":/out mbedded-cbor-docs sh -ec '
  echo "doxygen $(doxygen --version)"
  xsltproc --version | head -1
  xml=/tmp/doxygen/xml
  cd /src
  { cat Doxyfile; echo "OUTPUT_DIRECTORY = /tmp/doxygen"; } | doxygen -
  xsltproc tools/doc/pages.xsl $xml/index.xml > /tmp/names.txt
  tab=$(printf "\t")
  while IFS="$tab" read -r group name; do
    base=${name%%<*}
    base=${base% }
    file=${base##*::}
    case $file in
      "operator[]") file=operator_subscript ;;
    esac
    if [ ! -e doc/reference/$group/index.md ]; then
      echo "reference: the group $group of $name has no doc/reference/$group/index.md" >&2
      exit 1
    fi
    if [ -e doc/reference/$group/$file.md ]; then
      echo "reference: doc/reference/$group/$file.md and the comments of $name both give the page" >&2
      exit 1
    fi
    if [ -e /out/$group/$file.md ]; then
      echo "reference: two names give the page $group/$file.md; the second is $name" >&2
      exit 1
    fi
    mkdir -p /out/$group
    xsltproc --stringparam name "$name" --stringparam group "$group" tools/doc/reference.xsl $xml/index.xml > /out/$group/$file.md
  done < /tmp/names.txt'
