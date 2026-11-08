#!/bin/sh
# Checks the proof of tags 28 and 29 in theories/ with the Rocq prover of the image mbedded-cbor-rocq
# (containers/rocq.Containerfile). The build runs in a copy, so no build file enters the tree. The check fails when a
# file holds an admitted proof or a declaration that adds an assumption, or when a theorem of Assumptions.v rests on
# an axiom.
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
podman build -q -t mbedded-cbor-rocq -f "$tree/containers/rocq.Containerfile" "$tree/containers"
podman run --rm --network none -v "$tree/theories":/src:ro mbedded-cbor-rocq sh -ec '
  cp -r /src /tmp/theories
  cd /tmp/theories
  coq_makefile -f _CoqProject -o Makefile
  make -s
  if grep -nE "Admitted|admit|^[[:space:]]*(Local[[:space:]]+)?(Axiom|Axioms|Conjecture|Parameter|Parameters)[[:space:]]" *.v; then
    echo "the proof holds an admitted proof or an assumption"
    exit 1
  fi
  coqc -Q . MbeddedCbor Assumptions.v > assumptions.out
  cat assumptions.out
  theorems=$(grep -c "^Print Assumptions" Assumptions.v)
  closed=$(grep -c "^Closed under the global context$" assumptions.out)
  echo "$closed of $theorems theorems rest on no axiom"
  [ "$closed" -eq "$theorems" ]'
