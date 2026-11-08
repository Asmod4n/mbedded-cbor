#!/bin/sh
# Checks the proof of tags 28 and 29 in theories/ with the Rocq prover of the image mbedded-cbor-rocq
# (containers/rocq.Containerfile). The build runs in a copy, so no build file enters the tree. Each line
# "Closed under the global context" belongs to one theorem that rests on no axiom.
set -eu
tree=$(cd "$(dirname "$0")/../.." && pwd)
podman image exists mbedded-cbor-rocq || podman build -q -t mbedded-cbor-rocq -f "$tree/containers/rocq.Containerfile" "$tree/containers"
podman run --rm --network none -v "$tree/theories":/src:ro mbedded-cbor-rocq sh -ec '
  cp -r /src /tmp/theories
  cd /tmp/theories
  coq_makefile -f _CoqProject -o Makefile
  make -s
  if grep -n "Admitted\|^Axiom\|^Parameter" *.v; then echo "the proof holds an Admitted, an Axiom or a Parameter"; exit 1; fi
  coqc -Q . MbeddedCbor Assumptions.v'
