#!/bin/bash
# Writes the interop cases with the public writers of the library and decodes them with the CBOR decoders
# that Debian packages, in the image mbedded-cbor-interop (test/interop/Containerfile). The results stay in
# $CBOR_BUILD_ROOT/<branch>/interop. Exits 1 when a decoder refuses or misreads an item that the library
# wrote.
# usage: test/interop/run.sh
set -u
src=$(cd "$(dirname "$0")/../.." && pwd)
root=${CBOR_BUILD_ROOT:-/home/user/mbedded-cbor-build}
image=${CBOR_INTEROP_IMAGE:-mbedded-cbor-interop}
branch=$(git -C "$src" rev-parse --abbrev-ref HEAD | tr / -)
out=$root/$branch/interop
mkdir -p "$out"
podman image exists "$image" || podman build -t "$image" -f "$src/test/interop/Containerfile" "$src/test/interop" || exit 1
podman run --rm --network none -v "$src":/src:ro -v "$out":/out "$image" bash -c '
	set -u
	i=/src/test/interop
	flags="-O1 -Wall -Wextra -Werror -I/src/include"
	g++-16 -std=c++26 -freflection $flags $i/write.cpp -o /out/write &&
	g++-16 -std=c++23 $flags $i/tinycbor.cpp -o /out/tinycbor -ltinycbor &&
	g++-16 -std=c++23 $flags $i/libcbor.cpp -o /out/libcbor -lcbor &&
	g++-16 -std=c++23 $flags $i/jsoncons.cpp -o /out/jsoncons &&
	g++-16 -std=c++23 $flags $i/nlohmann.cpp -o /out/nlohmann || exit 1
	/out/write /src/test-vectors/appendix_a.json /src/cbor-test-vectors/tests /src/bench/docs >/out/cases.tsv || exit 1
	for d in tinycbor libcbor jsoncons nlohmann; do /out/$d </out/cases.tsv >/out/$d.tsv || exit 1; done
	python3 -I $i/cbor2_dump.py </out/cases.tsv >/out/cbor2.tsv || exit 1
	perl $i/cbor_xs_dump.pl </out/cases.tsv >/out/cbor_xs.tsv || exit 1
	for p in tinycbor:libtinycbor-dev libcbor:libcbor-dev jsoncons:libjsoncons-dev nlohmann:nlohmann-json3-dev \
		cbor2:python3-cbor2 cbor_xs:libcbor-xs-perl; do
		printf "%s\t%s %s\n" ${p%%:*} ${p#*:} "$(dpkg-query -W -f "\${Version}" ${p#*:})"
	done >/out/versions.tsv
	cd /out && python3 -I $i/check.py cases.tsv versions.tsv tinycbor=tinycbor.tsv libcbor=libcbor.tsv \
		jsoncons=jsoncons.tsv nlohmann=nlohmann.tsv cbor2=cbor2.tsv cbor_xs=cbor_xs.tsv
' 2>&1 | tee "$out/report.txt"
exit "${PIPESTATUS[0]}"
