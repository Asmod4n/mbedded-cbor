#!/bin/bash
set -eu
here=$(cd "$(dirname "$0")" && pwd)
root=$(dirname "$here")
build=${BUILD:?the build directory}
PROCESSES=${PROCESSES:-10}
MIN_TIME=${MIN_TIME:-0.2s}
docs=${DOCS:-"twitter floats records"}
arms=${ARMS:-"MB_DECODE LC_DECODE JC_DECODE READ TC_READ JC_READ LC_READ FB_READ FB_READ_V MP_READ MB_PATH MB_AT_PATH MB_AT_PATH_NAMED MB_AT_PATH_STATIC TC_PATH JC_PATH FB_PATH FB_PATH_V"}
compilers=${COMPILERS:-"g++-16 clang++-23"}
flags="-O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie -DNDEBUG -Werror"
export GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824:glibc.malloc.mmap_threshold=33554432:glibc.malloc.top_pad=268435456

arm_libraries() {
	case "$1" in
	LC_*) echo -lcbor ;;
	FB_*) echo -lflatbuffers ;;
	TC_*) echo -ltinycbor ;;
	esac
}

arm_directory() {
	case "$1" in
	MB_PATH_*) echo "$here/arms/MB_AT_PATH_${1#MB_PATH_}" ;;
	*) echo "$here/arms/$1" ;;
	esac
}

arm_read() {
	if [ -d "$(arm_directory "$1")" ]; then
		case "$1" in
		MB_PATH_*) echo -DARM_MB_PATH ;;
		MB_AT_PATH_*) echo -DARM_MB_AT_PATH ;;
		esac
	fi
}

arm_include() {
	local a=$1
	if [ -d "$(arm_directory "$a")" ]; then
		if [ ! -d "$build/include-$a" ]; then
			cp -r "$root/include" "$build/include-$a"
			cp -r "$(arm_directory "$a")/." "$build/include-$a/"
		fi
		echo "$build/include-$a"
	else
		echo "$root/include"
	fi
}

compile() {
	local cc=$1 a=$2 d=$3 out=$4
	"$cc" -std=c++23 $flags -DDOCTEST_CONFIG_DISABLE -DARM_$a $(arm_read "$a") -DARM_NAME="\"$a\"" -DDOC_$d \
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

steal() {
	local user nice system idle iowait irq softirq stolen rest
	read -r _ user nice system idle iowait irq softirq stolen rest < /proc/stat
	echo "$stolen $((user + nice + system + idle + iowait + irq + softirq + stolen))"
}
for b in "$build"/bin/*; do
	echo "$(basename "$b") $(readelf -h "$b" | grep -o 'Type: *[A-Z]*' | tr -s ' ')"
done > "$build/out/elf_type"
cat /proc/loadavg > "$build/out/loadavg_start"
i=1
while [ "$i" -le "$PROCESSES" ]; do
	s0=$(steal)
	t0=$(date +%s.%N)
	ls "$build"/bin | shuf > "$build/out/order.$i"
	while read -r n; do
		b=$build/bin/$n
		"$b" --benchmark_min_time="$MIN_TIME" --benchmark_out_format=json \
			--benchmark_out="$build/out/$i.$n.json" < /dev/null > /dev/null 2>> "$build/out/err.txt" ||
			echo "$n" >> "$build/out/failed.txt"
	done < "$build/out/order.$i"
	s1=$(steal)
	t1=$(date +%s.%N)
	echo "$i $t0 $t1 $s0 $s1" >> "$build/out/rounds"
	i=$((i + 1))
done
cat /proc/loadavg > "$build/out/loadavg_end"
