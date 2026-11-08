#!/bin/bash
# The gate of a bench: what runs now, from vmstat and ps --sort=-pcpu. The load average is not read.
# usage: source gate.sh; gate (exit status 3 when the machine is busy). GATE_MAX_BUSY_PERCENT, GATE_MAX_STEAL_PERCENT and
# GATE_SECONDS (the samples of vmstat) set the limits.
GATE_MAX_BUSY_PERCENT=${GATE_MAX_BUSY_PERCENT:-10}
GATE_MAX_STEAL_PERCENT=${GATE_MAX_STEAL_PERCENT:-0}
GATE_SECONDS=${GATE_SECONDS:-5}

gate() {
	local vm ps_out
	vm=$(vmstat 1 "$GATE_SECONDS")
	ps_out=$(ps -eo pid,user,pcpu,ni,comm --sort=-pcpu | head -n 8)
	printf 'gate: vmstat 1 %s\n%s\ngate: ps --sort=-pcpu (top 7)\n%s\n' "$GATE_SECONDS" "$vm" "$ps_out"
	python3 -I -c '
import sys
vm = sys.argv[1].splitlines()
head = vm[1].split()
rows = [list(map(int, r.split())) for r in vm[3:]]
col = lambda n: sum(r[head.index(n)] for r in rows) / len(rows)
busy = 100 - col("id")
steal = col("st") if "st" in head else 0.0
ok = busy <= float(sys.argv[2]) and steal <= float(sys.argv[4])
print("gate: busy %.1f%% of all cpus (limit %s%%), steal %.1f%%, iowait %.1f%%: %s" % (busy, sys.argv[2], steal, col("wa"), "idle enough" if ok else "NOT idle enough"))
for line in sys.argv[3].splitlines()[1:]:
    f = line.split()
    if float(f[2]) >= 5.0 and f[4] not in ("ps", "head", "vmstat", "run-desktop.sh", "function.sh"):
        print("gate: busy process %s user %s pcpu %s ni %s %s" % (f[0], f[1], f[2], f[3], " ".join(f[4:])))
sys.exit(0 if ok else 3)
' "$vm" "$GATE_MAX_BUSY_PERCENT" "$ps_out" "$GATE_MAX_STEAL_PERCENT"
}
