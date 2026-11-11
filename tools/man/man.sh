#!/bin/sh
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
out=${1:?output directory}
mkdir -p "$out"
out=$(cd "$out" && pwd)
version=$(sed -n 's/^project(mbedded-cbor VERSION \([0-9.]*\) .*/\1/p' "$tree/CMakeLists.txt")
podman run --rm --network none -e VERSION="$version" -v "$tree":/src:ro -v "$out":/out mbedded-cbor-docs sh -ec '
  echo "doxygen $(doxygen --version)"
  cd /src
  { cat tools/man/Doxyfile; echo "OUTPUT_DIRECTORY = /tmp/doxygen"; } | doxygen -
  mkdir -p /out/man3
  for page in /tmp/doxygen/man/man3/*.3; do
    case ${page##*/} in
      *.hpp.3 | *" "* | std_*) ;;
      *) perl /src/tools/man/split_requires_clause.pl < "$page" | perl /src/tools/man/strip_template_arguments.pl > "/out/man3/${page##*/}" ;;
    esac
  done
  mandoc -T lint -W error /out/man3/*.3
  status=0
  mandoc -T lint -W warning /out/man3/*.3 || status=$?
  echo "mandoc -T lint -W warning: status $status"'
