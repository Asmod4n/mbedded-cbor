#!/bin/bash
# Builds and runs the tests in the image mbedded-cbor-tw, then the tests of
# test/cmake, which install the library and build programs that use it.
# usage: test/run.sh check|one <pattern>|fast|full|<build>...
#   check: g++ 16 with reflection at -O0 and every test, the run after each change. It is the default.
#   one <pattern>: the build check, and only the test cases and ctest tests whose names contain <pattern>.
#   fast: gcc-release clang-asan
#   full: gcc-asan gcc-release clang-asan clang-release clang-libcxx gcc15-release
#   gcc15-release builds with g++ 15, the floor of the library, in the image of containers/tumbleweed-gcc15.Containerfile.
#   tsan, gcc-describe, gcc-describe-bundled and wasm are run by name and not in fast or full.
#   gcc-describe-bundled is gcc-describe with CMake told not to find Boost, so the tests take the copy in third_party.
#   gcc-describe builds with g++ and no reflection, so the tests take the path of the descriptions and Boost.PFR.
#   wasm compiles test/describe.cpp for wasm32-wasip1 in the image mbedded-cbor-wasm and does not run it: the
#   libc++ of Debian for wasm32 has no std::basic_fstream, which the runner of doctest needs.
#   The compile_fail tests run in check, gcc-release, clang-release, gcc15-release and gcc-describe only.
#   full also runs test/interop/run.sh, which decodes the output of the library with other decoders.
# Each build keeps its directory in $CBOR_BUILD_ROOT/<branch>/<build>,
# and every build shares the compiler cache in $CCACHE_HOST_DIR.
# The builds run one after the other, and each runs as many jobs as there are processors.
set -u
src=$(cd "$(dirname "$0")/.." && pwd)
root=${CBOR_BUILD_ROOT:-/home/user/mbedded-cbor-build}
cache=${CCACHE_HOST_DIR:-/home/user/.ccache}
image=${CBOR_IMAGE:-mbedded-cbor-tw}
branch=$(git -C "$src" rev-parse --abbrev-ref HEAD | tr / -)
parallel_level=$(nproc)

interop=0
[ "${1:-check}" = full ] && interop=1
pattern=
case ${1:-check} in
check) set -- check ;;
one) pattern=${2:?one takes a pattern}; set -- check ;;
fast) set -- gcc-release clang-asan ;;
full) set -- gcc-asan gcc-release clang-asan clang-release clang-libcxx gcc15-release ;;
esac

asan="-Og -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
tsan="-Og -fsanitize=thread -fno-omit-frame-pointer"
pch_clang="-Xclang -fno-pch-timestamp -Wlifetime-safety -Wno-lifetime-safety-invalidation"

one() {
	local n=$1 cxx type flags options= img=$image file= compile_fail=ON
	case $n in
	check) cxx=g++-16 type=Debug flags=-O0 ;;
	gcc-asan) cxx=g++-16 type=Debug flags=$asan compile_fail=OFF ;;
	gcc-release) cxx=g++-16 type=Release flags= ;;
	gcc-describe) cxx=g++-16 type=Release flags= options=-DCBOR_HAS_FREFLECTION=OFF ;;
	gcc15-release) cxx=g++-15 type=Release flags= img=mbedded-cbor-gcc15 file=tumbleweed-gcc15 ;;
	gcc-describe-bundled) cxx=g++-16 type=Release flags= options="-DCBOR_HAS_FREFLECTION=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Boost=ON" compile_fail=OFF ;;
	clang-asan) cxx=clang++-23 type=Debug flags="$asan $pch_clang" compile_fail=OFF ;;
	clang-release) cxx=clang++-23 type=Release flags=$pch_clang ;;
	clang-libcxx) cxx=clang++-23 type=Release flags="-stdlib=libc++ $pch_clang" compile_fail=OFF ;;
	tsan) cxx=clang++-23 type=Debug flags="$tsan $pch_clang" compile_fail=OFF ;;
	wasm)
		mkdir -p "$root/$branch/$n"
		podman run --rm --network none -v "$src":/src:ro -v "$root/$branch/$n":/b mbedded-cbor-wasm bash -c '
		s=${EPOCHREALTIME/./}
		clang++-23 --target=wasm32-wasip1 -std=gnu++23 -O2 -flto -fno-exceptions -Wall -Wextra -Werror \
			-idirafter /usr/include -I/src/include -DDOCTEST_CONFIG_NO_MULTITHREADING -DDOCTEST_CONFIG_NO_POSIX_SIGNALS \
			-DDOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS -DDOCTEST_COUNTER=__COUNTER__ \
			-c /src/test/describe.cpp -o /b/describe.o >/b/build.log 2>&1
		r=$?; b=${EPOCHREALTIME/./}
		printf "%-14s rc=%s build=%ds compiled, not run\n" wasm $r $(((b - s) / 1000000))
		exit $r' >"$root/$branch/$n.result" 2>&1
		return ;;
	*) echo "unknown build $n" >&2; return 2 ;;
	esac
	mkdir -p "$root/$branch/$n" "$cache"
	if [ -n "$file" ] && ! podman image exists "$img"; then
		podman build -q -t "$img" -f "$src/containers/$file.Containerfile" "$src/containers" >/dev/null || return 1
	fi
	podman run --rm --network none \
		-v "$src":/src:ro -v "$root/$branch/$n":/b -v "$cache":/ccache \
		-e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=9G -e CMAKE_BUILD_PARALLEL_LEVEL="$parallel_level" -e CBOR_TEST_PATTERN="$pattern" \
		-e CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime \
		"$img" bash -c '
		s=${EPOCHREALTIME/./}
		cmake -S /src -B /b -G Ninja -DCMAKE_BUILD_TYPE='"$type"' \
			-DCMAKE_CXX_COMPILER='"$cxx"' -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
			-DCMAKE_LINKER_TYPE=MOLD "-DCMAKE_CXX_FLAGS='"$flags"'" -DCBOR_COMPILE_FAIL='"$compile_fail"' '"$options"' >/b/configure.log 2>&1 &&
		cmake --build /b >/b/build.log 2>&1
		r=$?; b=${EPOCHREALTIME/./}
		rm -f /b/test.log /b/ctest.log; [ $r = 0 ] && { /b/mbedded-cbor-test ${CBOR_TEST_PATTERN:+"-tc=*$CBOR_TEST_PATTERN*"} >/b/test.log 2>&1; r=$?; }
		[ $r = 0 ] && { ctest --test-dir /b -j"$CMAKE_BUILD_PARALLEL_LEVEL" -R "^(cmake|clang_query|compile_fail|limits)_.*$CBOR_TEST_PATTERN" --output-on-failure >/b/ctest.log 2>&1; r=$?; }
		t=${EPOCHREALTIME/./}
		printf "%-14s rc=%s build=%ds test=%ds %s\n" '"$n"' $r \
			$(((b - s) / 1000000)) $(((t - b) / 1000000)) "$(tail -2 /b/test.log | head -1)"
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
		tail -n 30 "$root/$branch/$n/build.log" "$root/$branch/$n/test.log" "$root/$branch/$n/ctest.log"
done
if [ $interop = 1 ]; then
	"$src/test/interop/run.sh" | tail -40
	[ "${PIPESTATUS[0]}" = 0 ] || rc=1
fi
end=${EPOCHREALTIME/./}
printf "total %ds\n" $(((end - start) / 1000000))
exit $rc
