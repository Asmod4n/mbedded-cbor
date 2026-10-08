import collections
import glob
import hashlib
import json
import os
import platform
import statistics
import subprocess
import sys

build, root, stamp, flags, processes, min_time = sys.argv[1:7]


def run(*cmd):
    r = subprocess.run(cmd, capture_output=True, text=True)
    return (r.stdout + r.stderr).strip()


def read(path):
    return open(path).read().strip() if os.path.exists(path) else ""


def cpu():
    model, flags_line = "", ""
    with open("/proc/cpuinfo") as f:
        for line in f:
            if line.startswith("model name") and not model:
                model = line.split(":", 1)[1].strip()
            if line.startswith("flags") and not flags_line:
                flags_line = line.split(":", 1)[1].strip()
    return {"model": model, "flags_sha256": hashlib.sha256(flags_line.encode()).hexdigest(), "cpus": os.cpu_count(),
            "l3": read("/sys/devices/system/cpu/cpu0/cache/index3/size")}


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
        "check": context[name].get("check"),
        "copies": context[name].get("copies"),
    }

steal = []
for line in read(os.path.join(build, "out", "rounds")).splitlines():
    f = line.split()
    i, t0, t1, st0, tot0, st1, tot1 = f[0], float(f[1]), float(f[2]), int(f[3]), int(f[4]), int(f[5]), int(f[6])
    steal.append({"round": int(i), "wall_s": t1 - t0, "steal_ticks": st1 - st0,
                  "steal_percent_of_cpu_time": 100.0 * (st1 - st0) / max(1, tot1 - tot0)})

failed = sorted(set(read(os.path.join(build, "out", "failed.txt")).split()))
sample = sorted(glob.glob(os.path.join(build, "bin", "*")))
packages = run("dpkg-query", "-W", "-f", "${Package} ${Version}\n", "g++-16", "clang-23", "libstdc++6", "libc6",
               "libbenchmark-dev", "libtinycbor0", "libjsoncons-dev", "libcbor0.10", "libflatbuffers-dev",
               "libmsgpack-cxx-dev")
print(json.dumps({
    "time": stamp,
    "commit": read(os.path.join(build, "commit")),
    "purpose": "mbedded-cbor runtime API against CBOR libraries that openSUSE Tumbleweed does not package, in Debian sid",
    "flags": flags,
    "processes": int(processes),
    "min_time": min_time,
    "resolved_march": run("g++-16", "-march=native", "-Q", "--help=target").split("-march=", 1)[1].split()[0],
    "compilers": [run("g++-16", "--version").splitlines()[0], run("clang++-23", "--version").splitlines()[0]],
    "packages": packages.splitlines(),
    "ldd_sample": {os.path.basename(p): run("ldd", p) for p in sample if "g++" in p and ".twitter." in p},
    "libc": run("ldd", "--version").splitlines()[0],
    "kernel": platform.release(),
    "scheduler": read("/sys/kernel/sched_ext/state") or "CFS/EEVDF (no sched_ext state)",
    "virtualization": "firecracker VM, podman container localhost/mbedded-cbor-sid-gb (debian:sid)",
    "cpu": cpu(),
    "uid": os.getuid(),
    "priority": read(os.path.join(build, "renice")),
    "threads": 1,
    "pinned": False,
    "cpu_left_to_the_machine": "each bench process is single-threaded; 3 of 4 cpus stay to the rest of the machine",
    "cold": "each process rotates over copies of the input that hold twice the L3 size",
    "loadavg_start": read(os.path.join(build, "out", "loadavg_start")),
    "loadavg_end": read(os.path.join(build, "out", "loadavg_end")),
    "run": {"rounds": steal,
            "steal_percent_of_cpu_time_max": max((r["steal_percent_of_cpu_time"] for r in steal), default=None)},
    "failed": failed,
    "arms": arms,
}, indent=1))
