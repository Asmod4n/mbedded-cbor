#!/bin/bash
set -u
arm_a=${SPLIT_ARM_A:-/home/user/wt-split-base}
arm_b=${SPLIT_ARM_B:-/home/user/wt-split}
root=${SPLIT_ROOT:-/home/user/split-bench}
image=${SPLIT_IMAGE:-mbedded-cbor-tw}
jobs=${SPLIT_JOBS:-3}
priority=${SPLIT_NICE:--5}
repetitions=${SPLIT_REPETITIONS:-3}
uid=65534

asan="-Og -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
pch_clang="-Xclang -fno-pch-timestamp -Wlifetime-safety -Wno-lifetime-safety-invalidation"

host_context() {
	{
		printf "date\t%s\n" "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
		printf "kernel\t%s\n" "$(uname -srvm)"
		printf "scheduler\t%s\n" "$(chrt -p $$ | head -1 | sed 's/.*: //')"
		printf "autogroup\t%s\n" "$(cat /proc/sys/kernel/sched_autogroup_enabled 2>&1)"
		printf "virtualization\t%s\n" "$(systemd-detect-virt 2>&1)"
		printf "hypervisor_flag\t%s\n" "$(grep -m1 -c -w hypervisor /proc/cpuinfo)"
		printf "cpu\t%s\n" "$(grep -m1 'model name' /proc/cpuinfo | sed 's/.*: //')"
		printf "cpu_flags_sha256\t%s\n" "$(grep -m1 '^flags' /proc/cpuinfo | sed 's/.*: //' | sha256sum | cut -d' ' -f1)"
		printf "processors\t%s\n" "$(nproc)"
		printf "podman\t%s\n" "$(podman --version)"
		printf "image\t%s %s\n" "$image" "$(podman image inspect --format '{{.Id}}' "$image")"
		printf "host_user\t%s\n" "$(id -un)"
		printf "container_user\t%s\n" "$uid"
		printf "jobs\t%s\n" "$jobs"
		printf "nice_requested\t%s\n" "$priority"
		printf "script_commit\t%s\n" "$(git -C "$arm_b" log -1 --format=%H -- bench/split-build.sh)"
	} >"$root/host-$1.tsv"
}

one() {
	local arm=$1 src=$2 n=$3 rep=$4 cxx type flags compile_fail
	case $n in
	gcc-release) cxx=g++-16 type=Release flags= compile_fail=ON ;;
	gcc-asan) cxx=g++-16 type=Debug flags=$asan compile_fail=OFF ;;
	clang-asan) cxx=clang++-23 type=Debug flags="$asan $pch_clang" compile_fail=OFF ;;
	*) echo "unknown build $n" >&2; return 2 ;;
	esac
	local dir=$root/$arm/$n/$rep logs=$root/logs/$arm-$n-$rep
	rm -rf "$dir"
	mkdir -p "$dir" "$root/logs"
	chown "$uid:$uid" "$dir"
	local commit dirty steal_before steal_after
	commit=$(git -C "$src" rev-parse HEAD)
	dirty=$(git -C "$src" status --porcelain | wc -l)
	steal_before=$(awk '/^cpu /{print $9}' /proc/stat)
	nice -n "$priority" podman run --rm --network none --user "$uid:$uid" \
		-e HOME=/tmp -e CCACHE_DISABLE=1 -e CXX_NAME="$cxx" -e BUILD_TYPE="$type" -e FLAGS="$flags" \
		-e COMPILE_FAIL="$compile_fail" -e JOBS="$jobs" \
		-v "$src":/src:ro -v "$dir":/b "$image" bash -c '
		cmake -S /src -B /b -G Ninja -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_CXX_COMPILER="$CXX_NAME" \
			-DCMAKE_LINKER_TYPE=MOLD "-DCMAKE_CXX_FLAGS=$FLAGS" -DCBOR_COMPILE_FAIL="$COMPILE_FAIL" >/b/configure.log 2>&1 ||
			{ echo "1 0 $(nice)" >/b/time; exit 1; }
		s=${EPOCHREALTIME/./}
		cmake --build /b --target mbedded-cbor-test -j"$JOBS" >/b/build.log 2>&1
		r=$?
		e=${EPOCHREALTIME/./}
		echo "$r $((e - s)) $(nice)" >/b/time
		t=/b/mbedded-cbor-test
		{
			printf "compiler\t%s\n" "$($CXX_NAME --version | head -1)"
			printf "comment\t%s\n" "$(readelf -p .comment $t 2>&1 | grep -o "\] .*" | sed "s/^\] *//" | sort -u | paste -sd";")"
			printf "libstdcxx\t%s\n" "$(readlink -f "$(ldd $t | awk "/libstdc\+\+/{print \$3}")")"
			printf "libc\t%s\n" "$(readlink -f "$(ldd $t | awk "/libc\.so/{print \$3}")")"
			printf "glibcxx_needed\t%s\n" "$(objdump -T $t | grep -o "GLIBCXX_[0-9.]*" | sort -uV | tail -1)"
			printf "glibc_needed\t%s\n" "$(objdump -T $t | grep -o "GLIBC_[0-9.]*" | sort -uV | tail -1)"
			case $CXX_NAME in
			g++*) printf "isa\t%s\n" "$($CXX_NAME $FLAGS -Q --help=target 2>/dev/null | grep -E "^ +-m(arch|tune)=" | tr -s " " | paste -sd" ")" ;;
			*) printf "isa\t%s\n" "$($CXX_NAME $FLAGS -### -c -x c++ /dev/null 2>&1 | grep -o "\"-target-cpu\" \"[^\"]*\"" | head -1)" ;;
			esac
			printf "flags\t%s\n" "$(grep -m1 "^CMAKE_CXX_FLAGS:" /b/CMakeCache.txt)"
			printf "flags_type\t%s\n" "$(grep -m1 "^CMAKE_CXX_FLAGS_${BUILD_TYPE^^}:" /b/CMakeCache.txt)"
		} >/b/context.tsv 2>&1
		exit $r'
	steal_after=$(awk '/^cpu /{print $9}' /proc/stat)
	local rc wall nice_value
	read -r rc wall nice_value <"$dir/time"
	local longest
	longest=$(awk 'NR > 1 && $4 ~ /\.o$/ { d = $2 - $1; s += d; if (d > m) { m = d; p = $4 } } END { printf "%d\t%s\t%d", m, p, s }' "$dir/.ninja_log")
	printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n" "$arm" "$n" "$rep" "$commit" "$dirty" "$rc" "$wall" "$nice_value" \
		"$longest" "$((steal_after - steal_before))" >>"$root/rows.tsv"
	mkdir -p "$logs"
	cp "$dir/.ninja_log" "$dir/time" "$dir/context.tsv" "$dir/build.log" "$dir/configure.log" "$logs/" 2>/dev/null
	rm -rf "$dir"
	return "$rc"
}

mkdir -p "$root"
chown "$uid:$uid" "$root"
[ -s "$root/rows.tsv" ] ||
	printf "arm\tbuild\trep\tcommit\tdirty_files\trc\twall_us\tnice\tlongest_ms\tlongest_object\tobjects_ms\tsteal_ticks\n" >"$root/rows.tsv"
host_context "$(echo "$*" | tr " " _)"
rc=0
for n in "$@"; do
	for rep in $(seq "$repetitions"); do
		one A "$arm_a" "$n" "$rep" || rc=1
		one B "$arm_b" "$n" "$rep" || rc=1
	done
done
exit $rc
