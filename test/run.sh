#!/bin/bash
# Builds and runs the tests in the image mbedded-cbor-tw.
# usage: test/run.sh fast|full|<build>...
#   fast: gcc-release clang-asan
#   full: gcc-asan gcc-release clang-asan clang-release clang-libcxx
# Each build keeps its directory in $CBOR_BUILD_ROOT/<branch>/<build>,
# and every build shares the compiler cache in $CCACHE_HOST_DIR.
# The builds run one after the other, and each uses every processor.
set -u
src=$(cd "$(dirname "$0")/.." && pwd)
root=${CBOR_BUILD_ROOT:-/home/user/mbedded-cbor-build}
cache=${CCACHE_HOST_DIR:-/home/user/.ccache}
image=${CBOR_IMAGE:-mbedded-cbor-tw}
branch=$(git -C "$src" rev-parse --abbrev-ref HEAD | tr / -)
cpus=$(nproc)

case ${1:-fast} in
fast) set -- gcc-release clang-asan ;;
full) set -- gcc-asan gcc-release clang-asan clang-release clang-libcxx ;;
esac

asan="-O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
pch_clang="-Xclang -fno-pch-timestamp"

one() {
	local n=$1 cxx type flags
	case $n in
	gcc-asan) cxx=g++-16 type=Debug flags=$asan ;;
	gcc-release) cxx=g++-16 type=Release flags= ;;
	clang-asan) cxx=clang++-23 type=Debug flags="$asan $pch_clang" ;;
	clang-release) cxx=clang++-23 type=Release flags=$pch_clang ;;
	clang-libcxx) cxx=clang++-23 type=Release flags="-stdlib=libc++ $pch_clang" ;;
	*) echo "unknown build $n" >&2; return 2 ;;
	esac
	mkdir -p "$root/$branch/$n" "$cache"
	podman run --rm --network none \
		-v "$src":/src:ro -v "$root/$branch/$n":/b -v "$cache":/ccache \
		-e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=2G \
		-e CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime \
		"$image" bash -c '
		s=${EPOCHREALTIME/./}
		cmake -S /src -B /b -G Ninja -DCMAKE_BUILD_TYPE='"$type"' \
			-DCMAKE_CXX_COMPILER='"$cxx"' -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
			-DCMAKE_LINKER_TYPE=MOLD "-DCMAKE_CXX_FLAGS='"$flags"'" >/b/configure.log 2>&1 &&
		cmake --build /b -- -j'"$cpus"' >/b/build.log 2>&1
		r=$?; b=${EPOCHREALTIME/./}
		[ $r = 0 ] && { /b/mbedded-cbor-test >/b/test.log 2>&1; r=$?; }
		t=${EPOCHREALTIME/./}
		printf "%-14s rc=%s build=%ds test=%ds %s\n" '"$n"' $r \
			$(((b - s) / 1000000)) $(((t - b) / 1000000)) "$(tail -2 /b/test.log 2>/dev/null | head -1)"
		exit $r' >"$root/$branch/$n.result" 2>&1
}

start=${EPOCHREALTIME/./}
rc=0
for n in "$@"; do
	one "$n" || rc=1
done
for n in "$@"; do
	cat "$root/$branch/$n.result"
	[ -s "$root/$branch/$n.result" ] && grep -q "rc=0" "$root/$branch/$n.result" ||
		tail -30 "$root/$branch/$n/build.log" "$root/$branch/$n/test.log" 2>/dev/null
done
end=${EPOCHREALTIME/./}
printf "total %ds\n" $(((end - start) / 1000000))
exit $rc
