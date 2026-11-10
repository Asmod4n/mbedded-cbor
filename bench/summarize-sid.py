import collections
import glob
import json
import os
import re
import statistics
import sys

build, stamp, flags, processes, min_time = sys.argv[1:6]


def read(path):
    return open(path).read().strip() if os.path.exists(path) else ""


def elf_type_count(path):
    count = collections.Counter()
    for line in read(path).splitlines():
        name, kind = line.split(" ", 1)
        count[kind + " " + name.split(".")[0]] += 1
    return dict(sorted(count.items()))


def pie(flags):
    return "-fPIE -pie" if {"-fPIE", "-pie"} <= set(flags.split()) else "default of the compiler"


times = collections.defaultdict(list)
context = {}
for path in glob.glob(os.path.join(build, "out", "round*", "*.json")):
    name = os.path.basename(path)[:-5]
    data = json.load(open(path))
    context.setdefault(name, data["context"])
    for b in data["benchmarks"]:
        times[name].append(b["real_time"])

arms = {}
for name, t in sorted(times.items()):
    arms[name] = {
        "repetitions_ns": t,
        "median_ns": statistics.median(t),
        "spread": max(t) / min(t) - 1,
        "check": context[name].get("check"),
        "copies": context[name].get("copies"),
    }

context_of_run = json.load(open(os.path.join(build, "out", "context.json")))
context_of_run.update(json.load(open(os.path.join(build, "out", "settings.json"))))
steal = context_of_run["rounds_detail"]

commit = read(os.path.join(build, "commit"))
if not re.fullmatch(r"[0-9a-f]{40}(-dirty)?", commit):
    sys.exit(f"{build}/commit holds {commit!r}, not a commit with its dirty mark")
print(json.dumps({
    "time": stamp,
    "commit": commit,
    "purpose": "mbedded-cbor runtime API against CBOR libraries that openSUSE Tumbleweed does not package, in Debian sid",
    "flags": flags,
    "pie": pie(flags),
    "elf_type_count": elf_type_count(os.path.join(build, "out", "elf_type")),
    "processes": int(processes),
    **context_of_run,
    "loadavg_start": read(os.path.join(build, "out", "loadavg_start")),
    "loadavg_end": read(os.path.join(build, "out", "loadavg_end")),
    "steal_percent_of_cpu_time_max": max((r["steal_percent_of_cpu_time"] for r in steal), default=None),
    "arms": arms,
}, indent=1))
