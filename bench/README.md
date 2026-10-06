# Benchmarks

Two benches measure mbedded-cbor against other libraries:

- `runtime.cpp`: the runtime API against libcbor, msgpack-cxx and
  FlexBuffers (and jsoncons and vladimirgamalyan/cbor when a checkout is
  given). Each arm encodes a top-level item into a fixed buffer, or reads every
  item and every byte of the top-level item. Each file in `docs/` holds
  one top-level item.
- `schema.cpp`: `cbor::schema<T>` against FlatBuffers and Cap'n Proto on
  carsales (`cars.json`, 1000 cars). Each arm encodes the lot, or reads
  every field of every car.

- `cwt.cpp`: the CWT of RFC 8392 A.3 read and written as structs
  through `cbor::databind<T>`: COSE_Sign1 under tag 18, then the claims
  in its payload. `CWT_OPS` chooses `READ` and `ENC`.

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

Show a result as tables:

    python3 bench/show.py bench/results/<file>.json

`summarize.py` is called by `run.sh` and writes the result file.

Only some arms, for example mbedded-cbor and msgpack-cxx without the
schema bench:

    ARMS="S READ MP_REUSE MP_READ" SCHEMA_OPS="" bash bench/run.sh

Settings: `ARMS`, `SCHEMA_OPS`, `GXX`, `CLANGXX`, `JOBS` (parallel builds, default: all cpus), `PROCESSES` (default 10), `MIN_TIME`
(default `0.2s`).

## Every arm of the runtime bench does the same work

Each read arm reads every item of the top-level item once and sums every
byte of every text string, byte string and map key, inside tags too.
No read arm verifies the input before it reads it. mbedded-cbor does
not check UTF-8. jsoncons delivers an end event for each array and
map; the arm counts it as one item.

Each encode arm writes every item of the top-level item into a buffer that
it keeps across iterations:

- S (mbedded-cbor), VG_RAW, MP_REUSE and FB_REUSE walk the same
  `bench::value` tree and call the encoder of the library item by
  item. The msgpack-cxx `object` tree keeps the body of an ext item
  as bytes, not as items, so MP_REUSE does not encode from it.
- LC_PREALLOC walks the `cbor_item_t` tree that `cbor_load` built,
  and JC_CLEAR the `jsoncons::json` tree that `decode_cbor` built.
  Both trees hold every item of the top-level item, and the walk is part of
  the cost of the library. jsoncons keeps no tag it does not know, so
  JC_CLEAR writes `cwt` without tag 18 (not checked here: jsoncons
  has no package).

msgpack and FlexBuffers have no tag. msgpack writes a tag as ext type
1 whose body is the tag number and the content, packed item by item on
each iteration. FlexBuffers writes a tag as a vector of the tag number
and the content. Each read arm then sees one item more per tag than
the CBOR arms: the tag number.

Neither has an integer below -2^63. For such a CBOR negative integer
with argument n, msgpack writes ext type 2 whose body is n as uint64,
and FlexBuffers a vector of one UInt n. `ints.cbor` holds 135 of them,
so these read arms see two items for each.

A FlexBuffers map has string keys only. A CBOR map whose keys are all
text strings becomes a FlexBuffers map; the builder sorts its keys. Any
other map becomes a vector of key, value, key, value. No key is
converted to a decimal string.

A CBOR simple value false, true or null becomes the bool or nil of
msgpack and FlexBuffers. The top-level items hold no other simple value.

Floats: CBOR arms write the width the library chooses. mbedded-cbor
and vladimirgamalyan/cbor write the shortest width that holds the
value exactly (half, single or double). libcbor keeps the width of the
loaded item. jsoncons writes single or double (not checked here).
msgpack `pack_double` writes 8 bytes always. FlexBuffers writes 4 bytes
where the double is exact as a float, else 8, and all elements of one
vector take the width of the widest; `floats.cbor` is one vector, so
every float takes 8 bytes.

The item count and the string byte sum of each top-level item, as each read
arm sees them, were compared once outside the timed loop with a
checker that includes `runtime.cpp`. READ and LC_READ see the CBOR
items exactly; MP_READ and FB_READ see one item more per tag and per
integer below -2^63, and the same string bytes.
