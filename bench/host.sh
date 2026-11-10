#!/bin/bash
# The host part of every bench: the run lock, the commit of the measured code, the host context and the container.
# usage: source host.sh
take_run_lock() {
	local root=$1
	mkdir -p "$root"
	exec 9> "$root/run.lock"
	flock -n 9 || { echo "another build or bench holds $root/run.lock" >&2; exit 2; }
}

name_bench_commit() {
	local repo=$1 commit
	shift
	commit=$(git -C "$repo" rev-parse HEAD)
	[ -z "$(git -C "$repo" status --porcelain -- "$@")" ] || commit="$commit-dirty"
	echo "$commit"
}

write_host_context() {
	local file=$1
	{
		echo "kernel=$(uname -r)"
		echo "virtualization=$(systemd-detect-virt 2>&1 | tr '\n' ' ' || true)"
		echo "host_container=$([ -e /run/.containerenv ] || [ -e /.dockerenv ] && echo yes || echo no)"
		if [ -r /sys/kernel/sched_ext/state ]; then
			echo "scheduler=sched_ext $(cat /sys/kernel/sched_ext/state)"
		else
			echo "scheduler=no sched_ext state (kernel default)"
		fi
		echo "host_user=$(id -un) $(id -u)"
		echo "cpus=$(nproc)"
		echo "level_two=$(cat /sys/devices/system/cpu/cpu0/cache/index2/size)"
	} > "$file"
}

run_in_bench_container() {
	local image=$1 run=$2 script=$3 step=$4 repo
	shift 4
	repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
	podman run --rm --network none --user 65534:65534 --cap-add SYS_NICE --init \
		-v "$run":/run-dir:U -v "$repo/bench":/bench:ro -v "$repo/cmake":/cmake:ro -e IMAGE="$image" "$@" \
		"$image" bash "/bench/$script" "$step"
}

build_and_measure_in_container() {
	local image=$1 run=$2 script=$3
	shift 3
	gate > "$run/gate-before-build" 2>&1 || { cat "$run/gate-before-build"; echo "the machine is busy; no run was made" >&2; exit 3; }
	cat "$run/gate-before-build"
	write_host_context "$run/host"
	run_in_bench_container "$image" "$run" "$script" build "$@"
	gate > "$run/gate-before-runs" 2>&1 || { cat "$run/gate-before-runs"; echo "the machine is busy after the build; no run was made" >&2; exit 3; }
	run_in_bench_container "$image" "$run" "$script" measure "$@"
}
