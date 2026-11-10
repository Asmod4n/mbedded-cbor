#!/bin/bash
# The part of bench/sid.sh that runs in the container: "build" makes one binary per compiler, document and arm of
# bench/runtime.cpp, "measure" runs them in rounds and writes /run-dir/out/result.json.
set -eu
dir=/run-dir
. /bench/round.sh
. /cmake/warnings.sh
PROCESSES=${PROCESSES:-10}
MIN_TIME=${MIN_TIME:-0.2s}
docs=${DOCS:-"twitter floats records"}
arms=${ARMS:-"MB_DECODE LC_DECODE JC_DECODE READ TC_READ JC_READ LC_READ FB_READ FB_READ_V MP_READ MB_PATH MB_AT_PATH TC_PATH JC_PATH FB_PATH FB_PATH_V"}
compilers=${COMPILERS:-"g++-16 clang++-23"}
flags="-O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie -DNDEBUG"

arm_libraries() {
	case "$1" in
	LC_*) echo -lcbor ;;
	FB_*) echo -lflatbuffers ;;
	TC_*) echo -ltinycbor ;;
	esac
}

compile() {
	local cc=$1 a=$2 d=$3 out=$4
	"$cc" -std=c++23 $flags $(warnings_for "$cc") -DDOCTEST_CONFIG_DISABLE -DARM_$a -DARM_NAME="\"$a\"" -DDOC_$d \
		-DDOC_PATH="\"/bench/docs/$d.cbor\"" -I/include \
		/bench/runtime.cpp $(arm_libraries "$a") -lbenchmark -lpthread -o "$out"
}

build() {
	local cc d a
	mkdir -p "$dir/bin" "$dir/out"
	for cc in $compilers; do
		for d in $docs; do
			for a in $arms; do
				spawn compile "$cc" "$a" "$d" "$dir/bin/$cc.$d.$a"
			done
			for a in ${AA:-}; do
				spawn compile "$cc" "$a" "$d" "$dir/bin/$cc.$d.${a}_AA"
			done
		done
	done
	finish
}

one_round() {
	local try=$1 n
	for n in $(ls "$dir/bin" | shuf); do
		$runner "$dir/bin/$n" --benchmark_min_time="$MIN_TIME" --benchmark_out_format=json \
			--benchmark_out="$try/$n.json" < /dev/null > /dev/null 2>> "$dir/out/err.txt" ||
			echo "$n" >> "$dir/out/failed.txt"
	done
}

measure() {
	local b
	refuse_root
	runner=$(ask_priority "$dir/out")
	echo "priority: $(cat "$dir/out/priority")"
	record_packages "$dir/out" g++-16 clang-23 libstdc++6 libc6 libbenchmark-dev libtinycbor0 libjsoncons-dev libcbor0.10 \
		libflatbuffers-dev libmsgpack-cxx-dev
	for b in "$dir"/bin/*; do
		echo "$(basename "$b") $(readelf -h "$b" | grep -o 'Type: *[A-Z]*' | tr -s ' ')"
	done > "$dir/out/elf_type"
	cat /proc/loadavg > "$dir/out/loadavg_start"
	record_settings "$dir/out" "$flags" "$PROCESSES" "$MIN_TIME" $compilers
	run_rounds "$PROCESSES" "$dir/out" one_round
	cat /proc/loadavg > "$dir/out/loadavg_end"
	python3 -I /bench/context.py "$dir" "$steal_limit_percent" $compilers -- $(ls "$dir"/bin/* | head -n 1) \
		> "$dir/out/context.json"
	python3 -I /bench/summarize-sid.py "$dir" "$STAMP" "$flags" "$PROCESSES" "$MIN_TIME" > "$dir/out/result.json"
}

"$1"
