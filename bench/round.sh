#!/bin/bash
# The parts of a bench that run beside the binaries: the root refusal, the priority, the build jobs, the steal and
# the rounds. A round counts when its steal is under steal_limit_percent of its cpu time.
# usage: source round.sh
steal_limit_percent=3
attempts_per_round=2
error_lines_shown=20

refuse_root() {
	[ "$(id -u)" != 0 ] || { echo "never run as root" >&2; exit 2; }
}

ask_priority() {
	local out=$1
	if [ "$(nice -n -10 nice 2>> "$out/nice.log")" = -10 ]; then
		echo "nice -10 granted" > "$out/priority"
		echo "nice -n -10"
	else
		echo "nice 0 (nice -10 refused: $(tr '\n' ' ' < "$out/nice.log"))" > "$out/priority"
	fi
}

build_jobs() {
	nproc
}

running=0
spawn() {
	if [ "$running" -ge "$(build_jobs)" ]; then
		wait -n || true
		running=$((running - 1))
	fi
	"$@" &
	running=$((running + 1))
}

finish() {
	while [ "$running" -gt 0 ]; do
		wait -n || true
		running=$((running - 1))
	done
}

read_steal() {
	local user nice system idle iowait irq softirq stolen rest
	read -r _ user nice system idle iowait irq softirq stolen rest < /proc/stat
	echo "$stolen $((user + nice + system + idle + iowait + irq + softirq + stolen))"
}

round_is_kept() {
	local stolen0=$1 total0=$2 stolen1=$3 total1=$4 total
	total=$((total1 - total0))
	[ "$total" -ge 1 ] || total=1
	[ $((100 * (stolen1 - stolen0))) -lt $((steal_limit_percent * total)) ]
}

run_rounds() {
	local rounds=$1 out=$2 body=$3 i=1 attempt=0 s0 s1 t0 t1
	while [ "$i" -le "$rounds" ] && [ "$attempt" -lt $((attempts_per_round * rounds)) ]; do
		attempt=$((attempt + 1))
		mkdir -p "$out/try$attempt"
		s0=$(read_steal)
		t0=$EPOCHREALTIME
		"$body" "$out/try$attempt" "$attempt"
		s1=$(read_steal)
		t1=$EPOCHREALTIME
		if round_is_kept $s0 $s1; then
			mv "$out/try$attempt" "$out/round$i"
			echo "$i $attempt $t0 $t1 $s0 $s1 kept" >> "$out/rounds"
			echo "round $i/$rounds kept"
			i=$((i + 1))
		else
			echo "$i $attempt $t0 $t1 $s0 $s1 discarded" >> "$out/rounds"
			echo "round $i/$rounds discarded: steal not under $steal_limit_percent %"
		fi
	done
}

record_packages() {
	local out=$1
	shift
	dpkg-query -W -f '${Package} ${Version}\n' "$@" > "$out/packages" 2>&1 || true
}

record_settings() {
	local out=$1 cxx settings=("$2" "$3" "$4")
	shift 4
	for cxx in "$@"; do
		settings+=("$cxx" "$(warnings_for "$cxx" | tr '\n' ' ')")
	done
	python3 -I -c 'import json, sys; print(json.dumps({"flags": sys.argv[1], "rounds": int(sys.argv[2]), "min_time": sys.argv[3],
		"warnings": dict(zip(sys.argv[4::2], sys.argv[5::2]))}))' "${settings[@]}" > "$out/settings.json"
}
