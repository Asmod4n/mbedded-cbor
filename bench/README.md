# run-desktop.sh

The script measures two commits of mbedded-cbor on a desktop.

## Usage

    bench/run-desktop.sh <commit-a> <commit-b>

Run it as a normal user. It refuses to run as root.
Close other programs first. The script does not pin the process.

## What it measures

- Arms: READ, MB_PATH (the lazy path) and MB_AT_PATH (at_path).
- Documents: twitter, floats, records and tag29.
- tag29 is an array of 2000 items: 50 items with tag 28, then 1950 items with tag 29.
  The path reads item 1999.
- Compilers: g++-16 and clang++-23. The script takes g++ and clang++ when these names are absent.
- Flags: -O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie -DNDEBUG.
  The environment variable FLAGS replaces them.

## Method

1. The gate runs `vmstat 1 5` and `ps --sort=-pcpu`. It does not read the load average.
   It prints what else is busy. The run stops with code 3 when the busy share is above
   10 percent of all cpus or the steal is above 0 percent.
2. The script exports include/ of each commit with `git archive`.
   Commit a is built twice: a and aa. Commit b is built once.
   bench/runtime.cpp, bench/value.hpp and bench/docs come from the working tree.
3. Each arm of each variant is its own binary. Builds use all cpus but one.
4. Each round runs every cell once per variant. The order of the variants rotates in each round.
   One cpu stays free. The process is not pinned. nice -10 is used when the system allows it.
5. The script prints one line per round.
6. The result is the median of the rounds for each binary, and the spread (maximum over minimum, minus one).
   B/A is b over a. A/A is aa over a, and it is the noise of the cell.
   A cell is marked when B/A differs from 1 by more than the A/A of the cell.

## Result

One file: `bench/results/<UTC time>.json`. The variable RESULTS_DIR changes the directory.
The file holds:

- the result of each round for each binary, the median and the spread;
- both commits, the commit of the bench sources with its dirty mark;
- the flags, the resolved instruction set, the compilers;
- the C++ library and the C library, read off a binary of each compiler;
- the kernel, the scheduler, the virtual machine, the container;
- the processor and a digest of its flags;
- the user, the priority, the number of threads, pinned or not;
- the gate output, the steal of each round;
- the error of each cell that did not compile.

The binaries are deleted at the end of the run.

## Variables

ROUNDS (10), MIN_TIME (0.2s), DOCS, ARMS, JOBS, GXX, CLANGXX, REPO,
RESULTS_DIR, GATE_MAX_BUSY_PERCENT (10), GATE_MAX_STEAL_PERCENT (0).
COMMIT_TREES names a directory with `<commit>/include` and `<commit>.id`; it replaces git.
