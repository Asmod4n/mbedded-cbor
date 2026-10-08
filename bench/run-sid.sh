#!/bin/bash
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
build=${BUILD:?the build directory}
PROCESSES=${PROCESSES:-10}
MIN_TIME=${MIN_TIME:-0.2s}
docs=${DOCS:-"twitter floats records"}
arms=${ARMS:-"MB_DECODE LC_DECODE JC_DECODE READ TC_READ JC_READ LC_READ FB_READ FB_READ_V MP_READ MB_PATH MB_AT_PATH MB_AT_PATH_NAMED MB_AT_PATH_OWNED MB_AT_PATH_STATIC TC_PATH JC_PATH FB_PATH FB_PATH_V"}
compilers=${COMPILERS:-"g++-16 clang++-23"}
flags="-O2 -march=native -falign-functions=64 -falign-loops=64 -DNDEBUG -Werror"
export GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824:glibc.malloc.mmap_threshold=33554432:glibc.malloc.top_pad=268435456

arm_libraries() {
	case "$1" in
	LC_*) echo -lcbor ;;
	FB_*) echo -lflatbuffers ;;
	TC_*) echo -ltinycbor ;;
	esac
}

arm_include() {
	local a=$1
	if [ -d "$here/arms/$a" ]; then
		if [ ! -d "$build/include-$a" ]; then
			cp -r "$root/include" "$build/include-$a"
			cp -r "$here/arms/$a/." "$build/include-$a/"
		fi
		echo "$build/include-$a"
	else
		echo "$root/include"
	fi
}

compile() {
	local cc=$1 a=$2 d=$3 out=$4
	"$cc" -std=c++23 $flags -DDOCTEST_CONFIG_DISABLE -DARM_$a -DARM_NAME="\"$a\"" -DDOC_$d \
		-DDOC_PATH="\"$here/docs/$d.cbor\"" -I"$(arm_include "$a")" \
		"$here/runtime.cpp" $(arm_libraries "$a") -lbenchmark -lpthread -o "$out"
}

mkdir -p "$build/bin" "$build/out"
[ -n "${RUN_ONLY:-}" ] && compilers=""
for a in $arms; do arm_include "$a" > /dev/null; done
pids=""
for cc in $compilers; do
	for d in $docs; do
		for a in $arms; do
			compile "$cc" "$a" "$d" "$build/bin/$cc.$d.$a" &
			pids="$pids $!"
			if [ "$(echo $pids | wc -w)" -ge 4 ]; then
				for p in $pids; do wait "$p"; done
				pids=""
			fi
		done
		for a in ${AA:-}; do
			compile "$cc" "$a" "$d" "$build/bin/$cc.$d.${a}_AA" &
			pids="$pids $!"
		done
	done
done
for p in $pids; do wait "$p"; done
[ -n "${BUILD_ONLY:-}" ] && exit 0

steal() { awk '/^cpu /{print $9, $2+$3+$4+$5+$6+$7+$8+$9}' /proc/stat; }
cat /proc/loadavg > "$build/out/loadavg_start"
i=1
while [ "$i" -le "$PROCESSES" ]; do
	s0=$(steal)
	t0=$(date +%s.%N)
	for b in "$build"/bin/*; do
		n=$(basename "$b")
		"$b" --benchmark_min_time="$MIN_TIME" --benchmark_out_format=json \
			--benchmark_out="$build/out/$i.$n.json" > /dev/null 2>> "$build/out/err.txt" ||
			echo "$n" >> "$build/out/failed.txt"
	done
	s1=$(steal)
	t1=$(date +%s.%N)
	echo "$i $t0 $t1 $s0 $s1" >> "$build/out/rounds"
	i=$((i + 1))
done
cat /proc/loadavg > "$build/out/loadavg_end"
