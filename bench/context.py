# Prints the context of a bench run as one JSON object: what the machine, the compilers and the binaries are.
# usage: python3 -I context.py <run-dir> <steal-limit-percent> <compiler>... -- <binary>...
import hashlib
import json
import os
import subprocess
import sys


def run(*cmd):
    try:
        r = subprocess.run(cmd, capture_output=True, text=True)
    except FileNotFoundError as e:
        return str(e)
    return (r.stdout + r.stderr).strip()


def read(path):
    return open(path).read().strip() if os.path.exists(path) else ""


def cpu():
    model, fl = "", ""
    for line in open("/proc/cpuinfo"):
        if line.startswith("model name") and not model:
            model = line.split(":", 1)[1].strip()
        if line.startswith("flags") and not fl:
            fl = line.split(":", 1)[1].strip()
    return {"model": model, "flags_sha256": hashlib.sha256(fl.encode()).hexdigest(), "cpus": os.cpu_count(),
            "l3": read("/sys/devices/system/cpu/cpu0/cache/index3/size")}


def binary_info(b):
    if not os.path.exists(b):
        return {}
    libs = {}
    for line in run("ldd", b).splitlines():
        p = line.split()
        if p and p[0].startswith(("libstdc++", "libc.so", "libc++", "libm", "libbenchmark")) and "=>" in p:
            libs[p[0]] = os.path.realpath(p[2])
    return {"binary": os.path.basename(b), "comment": run("readelf", "-p", ".comment", b), "libraries": libs}


def resolved_march(compilers):
    for c in compilers:
        if "g++" in os.path.basename(c):
            text = run(c, "-march=native", "-Q", "--help=target")
            if "-march=" in text:
                return text.split("-march=", 1)[1].split()[0]
    return ""


def rounds_detail(path):
    rounds = []
    for line in read(path).splitlines():
        i, attempt, t0, t1, st0, tot0, st1, tot1, kept = line.split()
        rounds.append({"round": int(i), "attempt": int(attempt), "wall_s": float(t1) - float(t0), "kept": kept == "kept",
                       "steal_percent_of_cpu_time": 100.0 * (int(st1) - int(st0)) / max(1, int(tot1) - int(tot0))})
    return rounds


stderr_text_bytes = 4000
d, steal_limit = sys.argv[1:3]
rest = sys.argv[3:]
compilers = rest[:rest.index("--")] if "--" in rest else rest
binaries = rest[rest.index("--") + 1:] if "--" in rest else []
host = dict(l.split("=", 1) for l in read(os.path.join(d, "host")).splitlines() if "=" in l)
out = os.path.join(d, "out")
print(json.dumps({
    "resolved_march": resolved_march(compilers),
    "compilers": [run(c, "--version").splitlines()[0] for c in compilers],
    "binaries": {os.path.basename(b): binary_info(b) for b in binaries},
    "libc": run("ldd", "--version").splitlines()[0],
    "level_two_cache": host.get("level_two"),
    "kernel": host.get("kernel"),
    "scheduler": host.get("scheduler"),
    "virtualization": host.get("virtualization"),
    "host_in_container": host.get("host_container"),
    "host_user": host.get("host_user"),
    "container": "podman %s, user 65534, network none" % os.environ["IMAGE"] if "IMAGE" in os.environ else "none",
    "cpu": cpu(),
    "uid": os.getuid(),
    "priority": read(os.path.join(out, "priority")),
    "threads": 1,
    "pinned": False,
    "cpu_left_to_the_machine": "each bench process is single-threaded on %s cpus" % host.get("cpus"),
    "gate_before_build": read(os.path.join(d, "gate-before-build")).splitlines(),
    "gate_before_runs": read(os.path.join(d, "gate-before-runs")).splitlines(),
    "steal_limit_percent": int(steal_limit),
    "rounds_detail": rounds_detail(os.path.join(out, "rounds")),
    "failed": sorted(set(read(os.path.join(out, "failed.txt")).split())),
    "stderr_of_runs": read(os.path.join(out, "err.txt"))[:stderr_text_bytes],
    "packages": read(os.path.join(out, "packages")).splitlines(),
}, indent=1))
