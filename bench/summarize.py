import collections
import glob
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys

build, root, stamp, gxx, clangxx, flags, processes, min_time = sys.argv[1:9]


def run(*cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return (r.stdout + r.stderr).strip()


def read(path):
    return open(path).read().strip() if os.path.exists(path) else ""


def elf_type_count(path, script_compiler):
    count = collections.Counter()
    for line in read(path).splitlines():
        name, kind = line.split(" ", 1)
        compiler = name.split(".")[1] if name.startswith("rt.") else script_compiler
        count[kind + " " + compiler] += 1
    return dict(sorted(count.items()))


def pie(flags):
    return "-fPIE -pie" if {"-fPIE", "-pie"} <= set(flags.split()) else "default of the compiler"


def cpu():
    model, flags_line = "", ""
    with open("/proc/cpuinfo") as f:
        for line in f:
            if line.startswith("model name") and not model:
                model = line.split(":", 1)[1].strip()
            if line.startswith("flags") and not flags_line:
                flags_line = line.split(":", 1)[1].strip()
    return {"model": model, "flags_sha256": hashlib.sha256(flags_line.encode()).hexdigest(), "cpus": os.cpu_count()}


times = collections.defaultdict(list)
context = {}
for path in glob.glob(os.path.join(build, "out", "*.json")):
    name = os.path.basename(path)[:-5].split(".", 1)[1]
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
        "same": context[name].get("same"),
    }

failed_path = os.path.join(build, "out", "failed.txt")
failed = sorted(set(open(failed_path).read().split())) if os.path.exists(failed_path) else []
commit = run("git", "-C", root, "rev-parse", "HEAD")
dirty = run("git", "-C", root, "status", "--porcelain") != ""
sample = next(iter(glob.glob(os.path.join(build, "rt.*"))), "")
print(json.dumps({
    "time": stamp,
    "commit": commit + ("-dirty" if dirty else ""),
    "flags": flags,
    "pie": pie(flags),
    "elf_type_count": elf_type_count(os.path.join(build, "out", "elf_type"), os.path.basename(gxx)),
    "processes": int(processes),
    "min_time": min_time,
    "compilers": {"gxx": run(gxx, "--version").splitlines()[0], "clangxx": run(clangxx, "--version").splitlines()[0]},
    "resolved_march": run(gxx, "-march=native", "-Q", "--help=target").split("-march=")[1].split()[0]
    if "-march=" in run(gxx, "-march=native", "-Q", "--help=target") else "",
    "libraries": run("ldd", sample) if sample else "",
    "libc": run("ldd", "--version").splitlines()[0],
    "kernel": platform.release(),
    "virtualization": run("systemd-detect-virt"),
    "cpu": cpu(),
    "user": os.getuid(),
    "nice": os.nice(0),
    "threads": 1,
    "arms": arms,
    "failed": failed,
}, indent=1))
