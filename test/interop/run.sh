#!/bin/bash
# Tests interop in both directions with the CBOR libraries that Debian packages, in the image mbedded-cbor-interop
# (test/interop/Containerfile). The library writes the interop cases with its public writers, and each other library
# decodes them. Each other library writes the value it decoded again with its own encoder, and the library reads
# those bytes with cbor::decode. The library writes cycles with cbor::encode_cycles, cbor2 and CBOR::XS write
# cycles with tags 28 and 29, and cbor2, CBOR::XS and cbor::decode_cycles read all of them. The results stay in
# $CBOR_BUILD_ROOT/<branch>/interop. Exits 1 when check.py reports a finding. Then runs the same JSONPath queries
# over the same documents in the library and in jsoncons::jsonpath, and exits 1 on a difference.
# usage: test/interop/run.sh
set -u
src=$(cd "$(dirname "$0")/../.." && pwd)
root=${CBOR_BUILD_ROOT:-/home/user/mbedded-cbor-build}
image=${CBOR_INTEROP_IMAGE:-mbedded-cbor-interop}
branch=$(git -C "$src" rev-parse --abbrev-ref HEAD | tr / -)
commit=$(git -C "$src" rev-parse --short HEAD)$(git -C "$src" diff --quiet HEAD || echo -dirty)
out=$root/$branch/interop
mkdir -p "$out"
podman image exists "$image" || podman build -t "$image" -f "$src/test/interop/Containerfile" "$src/test/interop" || exit 1
podman run --rm --network none -e commit="$commit" -v "$src":/src:ro -v "$out":/out "$image" bash -c '
	set -u -o pipefail
	i=/src/test/interop
	flags="-O1 -Wall -Wextra -Werror -I/src/include"
	libraries="tinycbor libcbor jsoncons nlohmann cbor2 cbor_xs"
	g++-16 -std=c++26 -freflection $flags $i/write.cpp -o /out/write &&
	g++-16 -std=c++26 -freflection $flags $i/read.cpp -o /out/read &&
	g++-16 -std=c++23 $flags $i/tinycbor.cpp -o /out/tinycbor -ltinycbor &&
	g++-16 -std=c++23 $flags $i/libcbor.cpp -o /out/libcbor -lcbor &&
	g++-16 -std=c++23 $flags $i/jsoncons.cpp -o /out/jsoncons &&
	g++-16 -std=c++23 $flags $i/nlohmann.cpp -o /out/nlohmann &&
	g++-16 -std=c++26 -freflection $flags $i/jsonpath.cpp -o /out/jsonpath || exit 1
	/out/write /src/test-vectors/appendix_a.json /src/cbor-test-vectors/tests /src/bench/docs /out/cycles.tsv \
		>/out/cases.tsv || exit 1
	python3 -I $i/cbor2_dump.py cycles-write >>/out/cycles.tsv &&
	perl $i/cbor_xs_dump.pl cycles-write >>/out/cycles.tsv || exit 1
	for d in tinycbor libcbor jsoncons nlohmann; do
		/out/$d </out/cases.tsv >/out/$d.tsv && /out/$d reencode </out/cases.tsv >/out/${d}_others.tsv || exit 1
	done
	python3 -I $i/cbor2_dump.py </out/cases.tsv >/out/cbor2.tsv &&
	python3 -I $i/cbor2_dump.py reencode </out/cases.tsv >/out/cbor2_others.tsv &&
	perl $i/cbor_xs_dump.pl </out/cases.tsv >/out/cbor_xs.tsv &&
	perl $i/cbor_xs_dump.pl reencode </out/cases.tsv >/out/cbor_xs_others.tsv || exit 1
	for d in $libraries; do
		/out/read others </out/${d}_others.tsv >/out/${d}_read.tsv &&
		cut -f1,3 /out/${d}_others.tsv | sed "s/\t/\t-\t/; s/$/\t-/" | /out/tinycbor >/out/${d}_witness.tsv || exit 1
	done
	python3 -I $i/cbor2_dump.py cycles-read </out/cycles.tsv >/out/cycles_cbor2.tsv &&
	perl $i/cbor_xs_dump.pl cycles-read </out/cycles.tsv >/out/cycles_cbor_xs.tsv &&
	/out/read cycles </out/cycles.tsv >/out/cycles_mbedded-cbor.tsv || exit 1
	for p in tinycbor:libtinycbor-dev libcbor:libcbor-dev jsoncons:libjsoncons-dev nlohmann:nlohmann-json3-dev \
		cbor2:python3-cbor2 cbor_xs:libcbor-xs-perl; do
		printf "%s\t%s %s\n" ${p%%:*} ${p#*:} "$(dpkg-query -W -f "\${Version}" ${p#*:})"
	done >/out/versions.tsv
	printf "mbedded-cbor\tcommit %s\n" "$commit" >>/out/versions.tsv
	cd /out && python3 -I $i/check.py /out $libraries
	decoders=$?
	/out/jsonpath
	paths=$?
	[ $decoders = 0 ] && [ $paths = 0 ]
' 2>&1 | tee "$out/report.txt"
exit "${PIPESTATUS[0]}"
