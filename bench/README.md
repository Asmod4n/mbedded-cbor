# run-desktop.sh

The script measures two commits of mbedded-cbor on a desktop.

## Usage

    bench/run-desktop.sh <commit-a> <commit-b>

Run it as a normal user. It refuses to run as root.
The image of containers/bench-desktop.Containerfile holds the compilers and Google Benchmark;
it is built with `podman build -t mbedded-cbor-bench-desktop -f containers/bench-desktop.Containerfile containers`
after the image mbedded-cbor-tw.
Close other programs first. The process runs on the cpu that the scheduler gives it.

## What it measures

- Arms: READ, MB_PATH (the lazy path), MB_AT_PATH (at_path) and MB_STRUCT (cbor::structs decode).
- Documents: twitter, floats and records. MB_STRUCT decodes twitter into a struct with C++26 reflection.
- Compilers: g++-16 and clang++-23. The script takes g++ and clang++ when these names are absent.
- Flags: -O2 -march=native -falign-functions=64 -falign-loops=64 -fPIE -pie -DNDEBUG.
  The environment variable FLAGS replaces them.
- Warnings: the lists cmake/warnings-always.txt, cmake/warnings-optimized.txt and, for clang++,
  cmake/warnings-clang.txt. They always apply, with -Werror. FLAGS does not carry them.

## Method

1. The gate runs `vmstat 1 5` and `ps --sort=-pcpu`.
   It prints what else is busy. The run stops with code 3 when the busy share is above
   10 percent of all cpus or the steal is above 0 percent.
2. The script exports include/ of each commit with `git archive`.
   Commit a is built twice: a and aa. Commit b is built once.
   bench/runtime.cpp, bench/value.hpp and bench/docs come from the working tree.
3. Each arm of each variant is its own binary. Builds run as many jobs as there are cpus.
4. The gate runs again before the first round. Each round runs every cell once per variant. The order of the
   variants rotates in each round. Each bench process is single-threaded. nice -10 is used when the system allows
   it. A round whose steal is not under 3 percent of its cpu time is discarded and run again.
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

ROUNDS (10), MIN_TIME (0.2s), DOCS, ARMS, GXX, CLANGXX, REPO,
RESULTS_DIR, GATE_MAX_BUSY_PERCENT (10), GATE_MAX_STEAL_PERCENT (0).
COMMIT_TREES names a directory with `<commit>/include` and `<commit>.id`; it replaces git.

# function.sh

The script measures the functions of one block for two commits, in podman.

## Usage

    NOISE_FLOOR_PERCENT=<percent> bench/function.sh <commit-a> <commit-b> <block>

`<block>` names the file `bench/function/<block>.cpp`. Each file holds one Google Benchmark per function of the block.
Each benchmark checks its output once against RFC 8949, then writes every item of a buffer of COLD_BYTES bytes in each
iteration. NOISE_FLOOR_PERCENT is the largest deviation of B/A from 1 that the script found on this machine with commit
a as commit b. The image is built with
`podman build -t mbedded-cbor-sid-bench -f containers/debian-sid-bench.Containerfile containers`.

## Method

1. The script holds the lock of test/run.sh from the first build to the result file.
2. The gate of gate.sh runs on the host before the build and before the first round.
3. The script exports include/ of each commit with `git archive`. The bench file comes from the working tree.
4. One container builds g++-16 and clang++-23 binaries for a, aa (commit a again) and b, as many at once as there are
   cpus. It uses a precompiled header of the standard headers and of Google Benchmark, ccache and mold. The warnings
   of cmake/warnings-*.txt and -Werror always apply. FLAGS does not carry them.
5. A second container runs the rounds as user 65534 with no network. Each round runs every binary once, in an order
   that rotates. A round whose steal is not under 3 percent of its cpu time is discarded and run again.
6. B differs from A when the two-tailed Mann-Whitney U test of compare.py (Google Benchmark) finds a difference at
   alpha 0.05, and B/A is further from 1 than A/A and than NOISE_FLOOR_PERCENT.

## Result

One file: `bench/results/function-<block>-<UTC time>.json`, with the context of run-desktop.sh and the output of
compare.py. The script prints one table with a row per function and a column per build.

## Variables

ROUNDS (9), MIN_TIME (0.05s), COLD_BYTES (twice the level 2 cache), FLAGS, GATE_SECONDS (2),
CBOR_BUILD_ROOT, CCACHE_HOST_DIR, BENCH_IMAGE, RESULTS_DIR.

# sid.sh

The script measures the arms of bench/runtime.cpp against the CBOR libraries of Debian sid, in podman.

## Usage

    bench/sid.sh

The image is the one of function.sh. The bench runs as user 65534 in the container.

## Method

1. The script holds the lock of test/run.sh from the first build to the result file.
2. The gate of gate.sh runs on the host before the build and before the first round.
3. One container builds one binary per compiler, document and arm, from include/ and bench/ of the working tree,
   as many at once as there are cpus. The warnings of cmake/warnings-*.txt and -Werror always apply.
4. A second container runs the rounds as user 65534 with no network. Each round runs every binary once, in a
   random order. A round whose steal is not under 3 percent of its cpu time is discarded and run again.

## Result

One file: `bench/results/sid-<UTC time>.json`. It names HEAD with a dirty mark when include/, bench/, cmake/ or
test/owned_copy.hpp has changes.

## Variables

PROCESSES (10, the rounds), MIN_TIME (0.2s), DOCS, ARMS, AA (arms built twice), COMPILERS, CBOR_BUILD_ROOT,
BENCH_IMAGE, RESULTS_DIR.
