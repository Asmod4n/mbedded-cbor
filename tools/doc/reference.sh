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
  if grep -rlE "refid=\"(group__[a-z]+|mbedded-cbor)\" kindref=\"compound\"" $xml > /tmp/bare.txt; then
    echo "reference: a comment names a group or the overview page by a bare word; write cbor::<name> or %<word>:" >&2
    cat /tmp/bare.txt >&2
    exit 1
  fi
  xsltproc tools/doc/pages.xsl $xml/index.xml > /tmp/names.txt
  tab=$(printf "\t")
  while IFS="$tab" read -r group name; do
    case $name in
      cbor::*\<*) file=$(printf %s "$name" | sed "s/^[^<]*< *\([A-Za-z_][A-Za-z0-9_]*\).*/\1/") ;;
      *) base=${name%%<*}; base=${base% }; file=${base##*::} ;;
    esac
    case $file in
      "operator[]") file=operator_subscript ;;
    esac
    if [ ! -e tools/site/reference/$group/index.md ]; then
      echo "reference: the group $group of $name has no tools/site/reference/$group/index.md" >&2
      exit 1
    fi
    if [ -e tools/site/reference/$group/$file.md ]; then
      echo "reference: tools/site/reference/$group/$file.md and the comments of $name both give the page" >&2
      exit 1
    fi
    if [ -e /out/$group/$file.md ]; then
      echo "reference: two names give the page $group/$file.md; the second is $name" >&2
      exit 1
    fi
    mkdir -p /out/$group /tmp/rows
    xsltproc --stringparam name "$name" --stringparam group "$group" tools/doc/reference.xsl $xml/index.xml > /out/$group/$file.md
    title=$(sed -n "1s/^# //p" /out/$group/$file.md | sed "s/^cbor:://; s/&/\&amp;/g; s/</\&lt;/g; s/>/\&gt;/g")
    brief=$(xsltproc --stringparam name "$name" --stringparam group "$group" --stringparam part brief tools/doc/reference.xsl $xml/index.xml)
    if [ -z "$brief" ]; then
      echo "reference: $name has no brief description" >&2
      exit 1
    fi
    printf "| [%s](%s.md) | %s |\n" "$title" "$file" "$(printf %s "$brief" | sed "s/|/\\\\|/g")" >> /tmp/rows/$group.md
  done < /tmp/names.txt
  for hand in tools/site/reference/*/index.md; do
    group=$(basename $(dirname $hand))
    if ! grep -q "^|---|---|\$" $hand; then
      echo "reference: $hand has no table" >&2
      exit 1
    fi
    for file in $(sed -n "s/^| \[[^]]*\](\([A-Za-z0-9_]*\)\.md) |.*/\1/p" $hand); do
      if [ -e /out/$group/$file.md ]; then
        echo "reference: $hand lists $file.md, which the comments give" >&2
        exit 1
      fi
    done
    touch /tmp/rows/$group.md
    sed "/^|---|---|\$/r /tmp/rows/$group.md" $hand > /out/$group/index.md
  done
  mkdir -p /out/groups
  for file in $xml/group__*.xml; do
    group=$(basename $file .xml)
    group=${group#group__}
    xsltproc --stringparam group "$group" tools/doc/group.xsl $xml/index.xml > /out/groups/$group.md
  done
  xsltproc --stringparam group mbedded-cbor tools/doc/group.xsl $xml/index.xml > /out/overview.md'
