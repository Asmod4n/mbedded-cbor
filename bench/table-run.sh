#!/bin/bash
# The part of bench/kinds-table.sh and bench/readme-table.sh that runs in the container: "build" makes one binary per
# compiler and arm of bench/compare.cpp, "measure" runs them in rounds and writes the context of the run.
set -eu
dir=/run-dir
. /bench/round.sh
. /cmake/warnings.sh
ROUNDS=${ROUNDS:-5}
MIN_TIME=${MIN_TIME:-3s}
arms=${ARMS:?the arms of compare.cpp}
compilers=${COMPILERS:-"g++-16 clang++-23"}
flags="-O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie -DNDEBUG"

std_of() {
	case $1 in
	g++*) echo "-std=c++26 -freflection" ;;
	*) echo "-std=c++23" ;;
	esac
}

compile() {
	local cxx=$1 arm=$2 op=${2%_AA} log=$dir/log/$1.$2.log
	if "$cxx" $(std_of "$cxx") $flags $(warnings_for "$cxx") -DDS_CARS -DOP_$op -DCARS_JSON="\"/bench/cars.json\"" \
		-DDOC_PATH="\"/bench/docs/cars.cbor\"" -I/include -isystem "$dir/gen" /bench/compare.cpp \
		"$dir/gen/carsales.capnp.$cxx.o" -lcapnp -lkj -lflatbuffers -ltinycbor -lbenchmark -lpthread \
		-o "$dir/bin/$cxx.$arm" > "$log" 2>&1; then
		rm -f "${log:?}"
	else
		echo "build failed: $cxx $arm"
		head -n "$error_lines_shown" "$log"
	fi
}

build() {
	local cxx arm
	mkdir -p "$dir/gen" "$dir/bin" "$dir/out" "$dir/log"
	flatc --cpp --gen-object-api -o "$dir/gen" /bench/carsales.fbs
	capnp compile -oc++:"$dir/gen" --src-prefix=/bench /bench/carsales.capnp
	for cxx in $compilers; do
		"$cxx" $(std_of "$cxx") $flags -isystem "$dir/gen" -c "$dir/gen/carsales.capnp.c++" -o "$dir/gen/carsales.capnp.$cxx.o"
		for arm in $arms; do
			spawn compile "$cxx" "$arm"
		done
	done
	finish
}

one_round() {
	local try=$1 b
	for b in $(ls "$dir/bin" | shuf); do
		$runner "$dir/bin/$b" --benchmark_min_time="$MIN_TIME" --benchmark_format=json > "$try/$b.json" \
			2>> "$dir/out/err.txt" || echo "$b" >> "$dir/out/failed.txt"
	done
}

measure() {
	local cxx first=
	refuse_root
	runner=$(ask_priority "$dir/out")
	echo "priority: $(cat "$dir/out/priority")"
	record_packages "$dir/out" libstdc++6 libc6 libbenchmark-dev libtinycbor-dev libjsoncons-dev libflatbuffers-dev \
		libcapnp-dev
	record_settings "$dir/out" "$flags" "$ROUNDS" "$MIN_TIME" $compilers
	run_rounds "$ROUNDS" "$dir/out" one_round
	for cxx in $compilers; do
		first="$first $(ls "$dir"/bin/"$cxx".* | head -n 1)"
	done
	python3 -I /bench/context.py "$dir" "$steal_limit_percent" $compilers -- $first > "$dir/out/context.json"
}

"$1"
