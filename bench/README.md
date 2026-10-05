# Benchmarks

Two benches measure mbedded-cbor against other libraries:

- `runtime.cpp`: the runtime API against libcbor, msgpack-cxx and
  FlexBuffers (and jsoncons and vladimirgamalyan/cbor when a checkout is
  given). Each arm encodes a document into a fixed buffer, or reads every
  item and every byte of the document. Documents are in `docs/`.
- `schema.cpp`: `cbor::schema<T>` against FlatBuffers and Cap'n Proto on
  carsales (`cars.json`, 1000 cars). Each arm encodes the lot, or reads
  every field of every car.

## Install (openSUSE Tumbleweed)

    sudo zypper install gcc16-c++ clang benchmark-devel libcbor-devel \
        msgpack-cxx-devel flatbuffers-devel capnproto libcapnp-devel \
        nlohmann_json-devel python3

`run.sh` checks these packages and names the ones that are missing.

jsoncons and vladimirgamalyan/cbor have no package. To add their arms,
clone them and give the include directory:

    JSONCONS_INCLUDE=~/src/jsoncons/include VG_INCLUDE=~/src/cbor bash bench/run.sh

## Run

    bash bench/run.sh

The script builds every arm as its own binary with g++-16 and clang++,
`-O2 -march=native`. It runs each binary as 10 processes, one after the
other, and takes the median over the processes. It writes one file to
`bench/results/<UTC time>.json` with every repetition and the context of
the run, and deletes the build.

Settings: `GXX`, `CLANGXX`, `JOBS` (parallel builds, default: all cpus), `PROCESSES` (default 10), `MIN_TIME`
(default `0.2s`).

msgpack-cxx cannot hold every integer of `ints.cbor`; these arms are
listed under `failed` in the result file.
