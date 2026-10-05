#!/bin/bash
set -eu
packages="gcc16-c++ clang benchmark-devel libcbor-devel msgpack-cxx-devel flatbuffers-devel capnproto libcapnp-devel nlohmann_json-devel python3"
missing=""
for p in $packages; do
	rpm -q --quiet --whatprovides "$p" || missing="$missing $p"
done
if [ -n "$missing" ]; then
	echo "missing packages:$missing" >&2
	echo "install them with: sudo zypper install$missing" >&2
	exit 1
fi
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
GXX=${GXX:-g++-16}
CLANGXX=${CLANGXX:-clang++}
PROCESSES=${PROCESSES:-10}
MIN_TIME=${MIN_TIME:-0.2s}
stamp=$(date -u +%Y%m%dT%H%M%SZ)
build=$(mktemp -d "${TMPDIR:-/tmp}/mbedded-cbor-bench.XXXXXX")
trap 'rm -rf "$build"' EXIT
flags="-O2 -march=native -DNDEBUG"
export GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824:glibc.malloc.mmap_threshold=33554432:glibc.malloc.top_pad=268435456

docs="cwt senml floats ints strings records twitter"
arms=${ARMS:-"S READ LC_PREALLOC LC_READ MP_REUSE MP_READ FB_REUSE FB_READ"}
[ -n "${JSONCONS_INCLUDE:-}" ] && arms="$arms JC_CLEAR JC_READ"
[ -n "${VG_INCLUDE:-}" ] && arms="$arms VG_RAW VG_READ"
schema_ops=${SCHEMA_OPS-"ENC DEC PATH FB_ENC FB_READ CP_ENC CP_READ"}

arm_libraries() {
	case "$1" in
	LC_*) echo -lcbor ;;
	FB_*) echo -lflatbuffers ;;
	esac
}
JOBS=${JOBS:-$(nproc)}
trap 'exit 1' TERM
trap 'echo "a build failed" >&2; kill -TERM 0' USR1
running=0
finish() {
	while [ "$running" -gt 0 ]; do
		wait -n
		running=$((running - 1))
	done
}
spawn() {
	if [ "$running" -ge "$JOBS" ]; then
		wait -n
		running=$((running - 1))
	fi
	( "$@" || kill -USR1 $$ ) &
	running=$((running + 1))
}

echo "building into $build"
extra=""
[ -n "${JSONCONS_INCLUDE:-}" ] && extra="$extra -I$JSONCONS_INCLUDE"
[ -n "${VG_INCLUDE:-}" ] && extra="$extra -I$VG_INCLUDE"
for cc in "$GXX" "$CLANGXX"; do
	tag=$(basename "$cc")
	for a in $arms; do
		for d in $docs; do
			spawn "$cc" -std=c++23 $flags -DDOCTEST_CONFIG_DISABLE -DARM_$a -DARM_NAME="\"$a\"" \
				-DDOC_PATH="\"$here/docs/$d.cbor\"" -I"$root/include" $extra \
				"$here/runtime.cpp" $(arm_libraries "$a") -lbenchmark -lpthread -o "$build/rt.$tag.$a.$d"
		done
	done
done
finish
flatc --cpp -o "$build" "$here/carsales.fbs"
cp "$here/carsales.capnp" "$build/"
(cd "$build" && capnp compile -oc++ carsales.capnp)
"$GXX" -std=c++26 $flags -c "$build/carsales.capnp.c++" $(pkg-config --cflags capnp) -o "$build/carsales.capnp.o"
for op in $schema_ops; do
	spawn "$GXX" -std=c++26 -freflection $flags -DOP_$op -DCARS_JSON="\"$here/cars.json\"" \
		-I"$root/include" -I"$build" "$here/schema.cpp" "$build/carsales.capnp.o" \
		$(pkg-config --cflags --libs capnp) -lflatbuffers -lbenchmark -lpthread -o "$build/sc.$op"
done
finish

echo "running $PROCESSES processes per binary"
mkdir -p "$build/out"
i=1
while [ "$i" -le "$PROCESSES" ]; do
	for b in "$build"/rt.* "$build"/sc.*; do
		[ -e "$b" ] || continue
		n=$(basename "$b")
		"$b" --benchmark_min_time="$MIN_TIME" --benchmark_out_format=json \
			--benchmark_out="$build/out/$i.$n.json" > "$build/out/last.txt" 2>> "$build/out/err.txt" ||
			echo "$n" >> "$build/out/failed.txt"
	done
	i=$((i + 1))
done

mkdir -p "$here/results"
python3 "$here/summarize.py" "$build" "$root" "$stamp" "$GXX" "$CLANGXX" "$flags" "$PROCESSES" "$MIN_TIME" \
	> "$here/results/$stamp.json"
echo "wrote $here/results/$stamp.json"
