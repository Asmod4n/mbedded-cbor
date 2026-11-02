#!/bin/bash
# Builds and runs the tests in the image mbedded-cbor-tw, then the tests of
# test/cmake, which install the library and build programs that use it.
# usage: test/run.sh check|one <pattern>|fast|full|iter <target>...|<build>...
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
#   iter <target>...: builds only the named targets of gcc-asan with CBOR_TEST_GROUPS=ON in $CBOR_BUILD_ROOT/iter/gcc-asan,
#   in a container that stays up between runs, and runs each test program among them.
#   iter check-changed: builds the test programs that a change touches and runs each program whose program or data
#   changed since its last run that passed. CBOR_TEST_PATTERN selects the test cases of these runs.
# Each build keeps its directory in $CBOR_BUILD_ROOT/<branch>/<build>,
# and every build shares the compiler cache in $CCACHE_HOST_DIR.
# The builds run one after the other. Each build compiles as many jobs at once as its share of the memory
# allows (cmake/job_pools.cmake). CBOR_CONCURRENT_BUILDS is the number of runs of this script at the same time;
# a second run without it stops.
set -u
src=$(cd "$(dirname "$0")/.." && pwd)
root=${CBOR_BUILD_ROOT:-/home/user/mbedded-cbor-build}
cache=${CCACHE_HOST_DIR:-/home/user/.ccache}
image=${CBOR_IMAGE:-mbedded-cbor-tw}
branch=$(git -C "$src" rev-parse --abbrev-ref HEAD | tr / -)
parallel_level=$(nproc)
concurrent_builds=${CBOR_CONCURRENT_BUILDS:-1}
mkdir -p "$root"
exec 9>"$root/run.lock"
if ! flock -n 9 && [ -z "${CBOR_CONCURRENT_BUILDS:-}" ]; then
	echo "another test/run.sh runs; set CBOR_CONCURRENT_BUILDS to the number of runs at the same time" >&2
	exit 2
fi

interop=0
[ "${1:-check}" = full ] && interop=1
pattern=
iter_targets=
case ${1:-check} in
iter) shift; iter_targets="${*:?iter takes targets}"; pattern=${CBOR_TEST_PATTERN:-}; set -- iter ;;
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
	gcc-release) cxx=g++-16 type=Release flags= options=-DCBOR_TEST_GROUPS=ON ;;
	gcc-describe) cxx=g++-16 type=Release flags= options=-DCBOR_HAS_FREFLECTION=OFF ;;
	gcc15-release) cxx=g++-15 type=Release flags= img=mbedded-cbor-gcc15 file=tumbleweed-gcc15 ;;
	gcc-describe-bundled) cxx=g++-16 type=Release flags= options="-DCBOR_HAS_FREFLECTION=OFF -DCMAKE_DISABLE_FIND_PACKAGE_Boost=ON" compile_fail=OFF ;;
	clang-asan) cxx=clang++-23 type=Debug flags="$asan $pch_clang" compile_fail=OFF ;;
	clang-release) cxx=clang++-23 type=Release flags=$pch_clang ;;
	clang-libcxx) cxx=clang++-23 type=Release flags="-stdlib=libc++ $pch_clang" compile_fail=OFF ;;
	tsan) cxx=clang++-23 type=Debug flags="$tsan $pch_clang" compile_fail=OFF ;;
	iter) iter; return ;;
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
		-e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=9G ${CCACHE_DISABLE:+-e CCACHE_DISABLE=$CCACHE_DISABLE} -e CMAKE_BUILD_PARALLEL_LEVEL="$parallel_level" -e CBOR_TEST_PATTERN="$pattern" \
		-e CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime \
		"$img" bash -c '
		s=${EPOCHREALTIME/./}
		cmake -S /src -B /b -G Ninja -DCMAKE_BUILD_TYPE='"$type"' \
			-DCMAKE_CXX_COMPILER='"$cxx"' -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
			-DCMAKE_LINKER_TYPE=MOLD "-DCMAKE_CXX_FLAGS='"$flags"'" -DCBOR_COMPILE_FAIL='"$compile_fail"' -DCBOR_CONCURRENT_BUILDS='"$concurrent_builds"' '"$options"' >/b/configure.log 2>&1 &&
		cmake --build /b >/b/build.log 2>&1
		r=$?; b=${EPOCHREALTIME/./}
		rm -f /b/test.log /b/ctest.log; [ $r = 0 ] && { /b/mbedded-cbor-test ${CBOR_TEST_PATTERN:+"-tc=*$CBOR_TEST_PATTERN*"} >/b/test.log 2>&1; r=$?; }
		jobs=$(sed -n "s/^CBOR_PARALLEL_COMPILE_JOBS:INTERNAL=//p" /b/CMakeCache.txt)
		[ $r = 0 ] && { ctest --test-dir /b -j"$jobs" -R "^(cmake|clang_query|compile_fail|limits)_.*$CBOR_TEST_PATTERN" --output-on-failure >/b/ctest.log 2>&1; r=$?; }
		t=${EPOCHREALTIME/./}
		printf "%-14s rc=%s build=%ds test=%ds %s\n" '"$n"' $r \
			$(((b - s) / 1000000)) $(((t - b) / 1000000)) "$(tail -2 /b/test.log | head -1)"
		exit $r' >"$root/$branch/$n.result" 2>&1
}

iter() {
	local dir=$root/iter/gcc-asan name=mbedded-cbor-iter-gcc-asan jobs=$((parallel_level - 1)) user
	[ $jobs -ge 1 ] || jobs=1
	mkdir -p "$dir" "$cache" "$root/$branch"
	if [ "$(id -u)" = 0 ]; then
		user="--user 65534:65534"
		find "$dir" "$cache" ! -user 65534 -exec chown 65534:65534 {} +
	else
		user="--userns=keep-id --user $(id -u):$(id -g)"
	fi
	if ! podman container exists "$name" ||
		[ "$(podman container inspect -f '{{.Image}} {{.State.Running}} {{range .Mounts}}{{if eq .Destination "/src"}}{{.Source}}{{end}}{{end}}' "$name")" != "$(podman image inspect -f '{{.Id}}' "$image") true $src" ]; then
		podman container exists "$name" && { podman rm -f "$name" >/dev/null || return 1; }
		podman run -d --name "$name" --network none $user \
			-v "$src":/src:ro -v "$dir":/b -v "$cache":/ccache \
			-e HOME=/tmp -e CCACHE_DIR=/ccache -e CCACHE_MAXSIZE=9G ${CCACHE_DISABLE:+-e CCACHE_DISABLE=$CCACHE_DISABLE} -e CMAKE_BUILD_PARALLEL_LEVEL="$jobs" -e CBOR_TEST_PATTERN="$pattern" \
			-e CCACHE_SLOPPINESS=pch_defines,time_macros,include_file_mtime,include_file_ctime \
			"$image" sleep infinity >/dev/null 9>&- || return 1
	fi
	podman exec -e CBOR_TEST_PATTERN="$pattern" "$name" bash -c '
		s=${EPOCHREALTIME/./}
		if [ ! -f /b/build.ninja ] || ! grep -q "^CBOR_TEST_GROUPS:BOOL=ON$" /b/CMakeCache.txt ||
			! grep -qxF "CBOR_TEST_PATTERN:STRING=$CBOR_TEST_PATTERN" /b/CMakeCache.txt ||
			! grep -qxF "CBOR_CONCURRENT_BUILDS:STRING='"$concurrent_builds"'" /b/CMakeCache.txt; then
			cmake -S /src -B /b -G Ninja -DCMAKE_BUILD_TYPE=Debug \
				-DCMAKE_CXX_COMPILER=g++-16 -DCMAKE_CXX_COMPILER_LAUNCHER=ccache \
				-DCMAKE_LINKER_TYPE=MOLD "-DCMAKE_CXX_FLAGS='"$asan"'" -DCBOR_COMPILE_FAIL=OFF -DCBOR_TEST_GROUPS=ON -DCBOR_CONCURRENT_BUILDS='"$concurrent_builds"' "-DCBOR_TEST_PATTERN=$CBOR_TEST_PATTERN" >/b/configure.log 2>&1 ||
				{ tail -n 30 /b/configure.log; exit 1; }
		fi
		ccache -z >/dev/null
		cmake --build /b --target '"$iter_targets"' -j'"$jobs"' >/b/build.log 2>&1
		r=$?; b=${EPOCHREALTIME/./}
		ccache -s
		printf "iter build rc=%s build=%ds\n" $r $(((b - s) / 1000000))
		grep -o "Running [^ ]*$" /b/build.log | sort | uniq -c
		[ $r = 0 ] || { tail -n 30 /b/build.log; exit $r; }
		for t in '"$iter_targets"'; do
			[ -f /b/$t ] && [ -x /b/$t ] || continue
			p=${EPOCHREALTIME/./}
			/b/$t ${CBOR_TEST_PATTERN:+"-tc=*$CBOR_TEST_PATTERN*"} >/b/$t.log 2>&1
			q=$?; e=${EPOCHREALTIME/./}
			printf "%s rc=%s test=%ds %s\n" $t $q $(((e - p) / 1000000)) "$(tail -2 /b/$t.log | head -1)"
			[ $q = 0 ] || { tail -n 30 /b/$t.log; r=$q; }
		done
		exit $r' >"$root/$branch/iter.result" 2>&1
}

first_error() {
	local at
	at=$(grep -m1 -n -E 'error:|FAILED:' "$1" | cut -d: -f1)
	[ -n "$at" ] && sed -n "$((at > 20 ? at - 20 : 1)),$((at + 30))p" "$1"
}

start=${EPOCHREALTIME/./}
rc=0
for n in "$@"; do
	one "$n" || rc=1
done
for n in "$@"; do
	cat "$root/$branch/$n.result"
	[ "$n" = iter ] && continue
	[ -s "$root/$branch/$n.result" ] && grep -q "rc=0" "$root/$branch/$n.result" ||
		{ first_error "$root/$branch/$n/build.log"; tail -n 30 "$root/$branch/$n/build.log" "$root/$branch/$n/test.log" "$root/$branch/$n/ctest.log"; }
done
if [ $interop = 1 ]; then
	"$src/test/interop/run.sh" | tail -40
	[ "${PIPESTATUS[0]}" = 0 ] || rc=1
fi
end=${EPOCHREALTIME/./}
printf "total %ds\n" $(((end - start) / 1000000))
exit $rc
