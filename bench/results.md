# Measurements

Each entry names the date, the commit, the build and the result.
The entry of 2026-10-03 for commit a61ee52 used a timing loop of its own and not Google Benchmark. Its numbers are not valid.

- 2026-10-03, CTAP2 response, 20000 messages, mbedded-cbor a61ee52.
  One binary per library and compiler. Our code was built with
  -O2 -march=x86-64-v4 -falign-functions=64 -falign-loops=64
  -Wall -Wextra -Wpedantic -Werror. The run was uid 1000, nice 0
  (priority not given), 3 cpus, not pinned, 3 passes in rotated order.
  Median of million messages per second, decode / encode:

  | arm | gcc 16 | clang |
  |---|---|---|
  | schema | 30.5 / 21.2 | no reflection |
  | flatbuffers | 27.8 / 6.6 | 20.3 / 6.8 |
  | vladimirgamalyan | 14.6 / 10.2 | 12.0 / 4.3 |
  | generic | 13.2 / 29.6 | no reflection |
  | protobuf | 9.5 / 10.7 | 10.3 / 11.2 |
  | capnp | 8.7 / 9.9 | 8.9 / 10.3 |
  | libcbor | 8.6 / 19.8 | 8.5 / 22.1 |
  | msgpack | 8.1 / 31.5 | 6.7 / 22.7 |
  | bson | 7.3 / 4.7 | 7.3 / 4.7 |
  | tinycbor | 4.9 / 10.6 | 4.9 / 8.7 |
  | simdjson | 4.5 / 7.2 | 4.6 / 7.0 |
  | qcbor | 4.4 / 4.0 | 4.3 / 4.5 |
  | jsoncons | 2.5 / 6.5 | 2.3 / 4.4 |

  No separate noise floor was measured for this run.


- 2026-10-03, key cache of lazy (simple-api 0518f6f, std::map per node
  in internal::document) against the same header without it, twitter.json
  1000 cold copies, 31 rounds alternating, us, gcc 16 / clang 23:
  search_metadata (one lookup): cache 148.2 / 143.0 (noise 146.4 /
  144.7), no cache 150.9 / 143.4. statuses (three lookups in each of
  100 statuses, each asked once): cache 182.7 / 182.8 (noise 183.4 /
  180.1), no cache 155.7 / 149.9. The cache costs 17 to 22 percent where
  no key is asked twice.

- 2026-10-03, wire messages, generic-reflection da68035: 20000 single
  messages each of SenML (RFC 8428), CWT in COSE_Sign1 (RFC 8392 A.3)
  and the CTAP2 getAssertion response; every library reads and writes
  each message alone. 21 rounds, medians, MB/s, gcc 16 (clang 23):
  - walk senml/cwt/ctap2: mbedded 698/6803/3613 (998/9003/4948);
    best other vg 649/6604/3267 with gcc, libcbor and fxamacker below.
  - into a C++ type: generic with views 519/6583/2720, with copies
    337/1132/1010; tree of mbedded 108/580/464, libcbor 71/865/491.
  - encode: generic 674/2231/1905, mbedded tree 378/1344/1674, best
    other libcbor or jsoncons 157/1236/716.

- Decision, 2026-10-03: cbor::generic reads and writes any CBOR item
  into a C++ type by reflection. The README says that it gives
  compatibility with CBOR from other programs, and that the schema form
  (cbor::encode, cbor::view, cbor::decode) is the faster one, because
  generic must parse every item and the schema form reads fields at
  fixed offsets.

- 2026-10-03, the skip of an item, four more forms against the old
  header, percent, gcc / clang. D (a one-byte argument read alone):
  ints -8 / -9, records +5 / 0, floats -2 / -1, strings +40 / +20. E
  (the same for strings only): records +2 / -38. F (an integer skipped
  without its argument): ints +1 / +11, records +4 / -34, floats -10 /
  -1. G (both loads, a select): ints -10 / -3, floats -7 / 0. clang
  loses records with every added branch in the loop; alignment and
  -fno-exceptions change nothing. The owner took D for the 40 percent.

- 2026-10-03, doc_end over strings, why libcbor leads. One binary per
  compiler, old and new header in it, 31 rounds alternating, medians of
  40 MB per sample, MB/s, gcc 16 / clang 23. The comparison bench reads
  2 MB twice per sample; there the old doc_end gives 6964 for strings,
  here 9376 to 11028 / 9046 to 10692 (the second arm of the same code
  is 15 percent faster: order, not code).
  - A short string first (head of 1 or 2 bytes, before the 8-byte
    load), at the top of the loop: strings +20 to +35 / +10 to +25;
    clang records -26, ints -9, floats -11; gcc floats -7.
  - The same test inside the 9-byte path: strings +22 to +41 / +8 to
    +26; gcc records -3, floats +5; clang records -25, ints -8, floats
    -11.
  - Neither is faster across the table, so neither entered the tree.

- 2026-10-03, main 12826f8, Tumbleweed, uid 1000, 3 of 4 cpus, not
  pinned, nice 0, the only processes of the user.
  - carsales, gcc 16 -O2 -march=x86-64-v4 -falign-*=64, 11 rounds
    alternating, medians, ns per car: encode into a span 91.0 (noise
    91.9), Cap'n Proto 83.5; read through view 50.3 (noise 49.3), Cap'n
    Proto 58.9; decode<T> into the native structs and sum 116.4.
  - Against tinycbor, QCBOR, libcbor, jsoncons, fxamacker and
    vladimirgamalyan/cbor 0fd6a48 (C++, a virtual call per byte; its
    walk skips strings in the buffer, its encode reads the test tree),
    31 rounds, medians, MB/s, gcc 16 / clang 23:
    - walk: mbedded fastest in records, ints and floats (gcc floats:
      vg 1415 against 1363); strings libcbor 9660 / 9503 and
      fxamacker 9216 / 8556 against 6964 / 6808.
    - tree: mbedded fastest except records with gcc and clang
      (jsoncons 171 / 150 against 158 / 132).
    - encode: mbedded fastest in every document, 1.2 to 4 times the
      next one.

- 2026-10-03, carsales read after 7f380a2 (a field is a fixed-extent
  span), same rules, 11 rounds alternating, ns per car: library 54.2
  (noise 53.4), prototype 53.4, Cap'n Proto 58.0. All sums right.

- 2026-10-03, carsales read, struct-encode 04695c8, same build and rules
  as the encode row, 11 rounds alternating, medians, ns per car: the
  library through at_path_compiled and cbor::array 66.6 (noise 66.7),
  the prototype reader 49.3, Cap'n Proto 57.2. All three sum all 20000
  lots right. The library loses 17 ns per car to the prototype; not
  examined yet.

- 2026-10-03, mruby-cbor a885a44, Path "$.items[*]" over 2000 elements,
  a read-only look into the kcache of the Lazy before each element is
  built (probe) against no look (base). Tumbleweed gcc 16 -O2
  -march=x86-64-v4 -falign-*=64, uid 1000, 3 of 4 cpus, nice 0, one
  mruby per arm, 11 rounds alternating, median of 9 inside each run, us.
  Elements built before the walk through Lazy#[] and #value:
  - a map of 11 fields: 0 %: base 7823, base again 7656, probe 7685.
    1 %: 7790 / 7954 / 7987. 10 %: 8019 / 8001 / 7330. 50 %: 10149 /
    10371 / 5013. 100 %: 11806 / 11650 / 474.
  - an integer: base 35 to 42, probe 52 with an empty cache, 111 to
    142 with any entry in it.

- 2026-10-03, struct-encode 9c82c87, Tumbleweed, uid 1000, 3 of 4
  cpus, not pinned, nice 0, the only processes of the user,
  -O2 -march=x86-64-v4 -falign-functions=64 -falign-loops=64.
  - carsales encode, gcc 16 with reflection, 11 rounds alternating,
    medians, ns per car: library 90.3, the same binary again 90.7
    (noise), Cap'n Proto 1.5.0 80.2. clang has no reflection, so no
    column. The library has no read over a list at run time, so no
    read number of the library exists; 54.9 of 2026-10-02 was the
    prototype.
  - twitter.json, 1000 cold copies, 31 rounds alternating, medians,
    us. gcc 16 / clang 23. search_metadata: simdjson On-Demand with a
    kept parser 206.9 / 190.7 (3589248 bytes kept), with a new parser
    209.3 / 191.7, mbedded-cbor lazy 152.6 / 147.5 (noise 156.9 /
    146.1, nothing kept). statuses: kept 203.1 / 195.4, new 200.1 /
    191.1, mbedded-cbor 157.1 / 149.3 (noise 157.8 / 151.3).

- 2026-10-02, carsales encode with the library (56491d1: one overflow check
  per list, heads stored into 8 bytes of padding that the writer
  gives and encode cuts off), 11 rounds alternating, ns per car:
  library 93.4 (noise 93.6), prototype with a live count 96.7,
  Cap'n Proto 87.4. The prototype reader, set to offsets from the
  start of the message, sums all 20000 lots right.

- Correction, 2026-10-02: the encode numbers of the prototype above
  (86.0 against Cap'n Proto 89.7, and 82.2 to 83.9 in the later rows)
  are wrong. gcc removed the counting pass of the prototype, because
  its result was never read. With a live count the prototype takes
  94.9 ns per car (plain adds) or 98.8 (ckd_add), and the library
  98.5; Cap'n Proto 87.6 to 90.2. Per byte the CBOR form writes 472.8
  bytes per car against 120.3, about 4.8 GB/s against 1.4 GB/s. The
  read numbers are not affected; reading counts nothing.

- 2026-10-02, interop: cbor::encode (ea535a6) wrote 50 parking lots of
  carsales (5351 cars). fxamacker/cbor (e1dfebc) read both items of
  each with its generic decoder and consumed every byte. A Go program
  that knows only the schema followed each offset and length into the
  second item and printed every field; its output is byte for byte the
  output of the C++ values, floats compared by their bits.

- 2026-10-02, schema encode of carsales, same build as before, 11
  rounds, ns per car: prototype with a char pointer 82.2 to 83.4
  (noise within 0.5); sizes with ckd_add and ckd_mul 83.0; a writer
  with resize_and_overwrite and memcpy 82.9; the same writer with
  std::ranges::copy for the heads and keys 94.5 to 96.9. Cap'n Proto
  90.2. Checked sizes and the writer cost nothing; std::ranges::copy
  of a span of char costs 12 ns per car with gcc 16 -O2.
  std::ranges::copy from a std::span<char const, N> into
  out.first<N>(), so the size is part of both types: 83.3, against
  memcpy 83.7 (noise 83.8) and the dynamic span 94.9.

- 2026-10-02, schema reader guards, same build as carsales, 11 rounds,
  ns per car: none 54.3 (noise 53.9); offsets only forward 54.3;
  forward and 512 MiB budget 54.5; forward and a budget of 8 times the
  message size 54.8. An attack of 945152 bytes, where 2000 cars point
  their wheels forward at one region (8612 wheels each): none 37.9 ms,
  forward 38.1 ms, 512 MiB 28.6 ms, 8 times the message 0.4 ms. A
  backward offset is refused by the forward rule.

- 2026-10-02, the traversal limit of Cap'n Proto (512 MiB, counted in
  bytes of texts and lists) in the schema reader, same build and
  machine as carsales: handle 54.2 ns per car against 53.3 without it,
  noise 53.5. An attack message of 945152 bytes, where each of 2000
  cars points its wheels at the whole second item (23051 wheels per
  car), takes 100.5 ms without the limit and 29.5 ms with it.

- 2026-10-02, carsales of Cap'n Proto (c++/src/benchmark, e866bdb),
  the same xorshift data, 20000 parking lots, 1994343 cars, so every
  message is cold. Tumbleweed gcc 16.2, -freflection -O2
  -march=x86-64-v4 -falign-functions=64 -falign-loops=64, uid 1000,
  3 cpus, not pinned, no raised priority, one binary per arm, 11 rounds
  alternating, medians, ns per car. CBOR with schema (prototype by
  reflection, keys on the wire): encode 86.0, handle 54.9, 472.8 bytes.
  Cap'n Proto 1.5.0 of Tumbleweed: encode 89.7, handle 67.6, 120.3
  bytes. Noise 86.0 against 86.4. Both sums match for every message;
  cbor2 reads the CBOR without the schema.

- 2026-10-02, twitter.json, search_metadata, 1000 cold copies, Google
  Benchmark 1.9.5, gcc 16.2 Tumbleweed, one binary per arm, 31 rounds:
  simdjson 5.0.2 On-Demand with a kept parser 199.5 us and 3505 KiB
  kept; with a new parser per document 209.1 us; mbedded-cbor lazy with
  the skip that loads 8 bytes 126.1 us and 1 KiB at its peak. In
  mruby, one cold run: mruby-cbor Path#at 154 us, simdjson On-Demand
  in C++ 244 us (median of 5).

- 2026-10-02, Tumbleweed gcc 16.2, uid 1000, 3 of 4 cpus, not pinned,
  one binary per arm, -O2 -march=x86-64-v4 -falign-functions=64
  -falign-loops=64, medians of 31, alternating:
  - Against tinycbor, QCBOR, libcbor, jsoncons, fxamacker/cbor (Go,
    GOAMD64=v4): encode is the fastest in every document. Walk loses
    records and strings to fxamacker and libcbor. Tree after the size
    for array_decode and map_decode wins every document with gcc 14 on
    Debian; with clang 21 libcbor wins three of four.
  - The size for the host: +13 to +31 % tree, records -3 to -7 % with
    glibc malloc only; faster in every document with mimalloc 3.0.1,
    jemalloc 5.3.0 and tcmalloc of gperftools 2.16, all of Debian trixie. mimalloc 3.3.2 of
    Tumbleweed crashes every C++ program in std::locale at start.
  - Encode with a block of 16 KiB and a head without a branch: records
    760 to 1190 MB/s, ints 390 to 920 MB/s.
  - records encode: dynamic host 1156 MB/s, reflection through
    cbor::encoder 1213 MB/s, reflection with a local cursor 2700 MB/s.
    The gain is the cursor, not the reflection.
  - memcpy, 1/2/3 threads: 256 MiB 8.4/13/21 GB/s, 2 MiB 15/51/109
    GB/s. The VM has 260 MiB L3, so this is not RAM bandwidth.

- libFuzzer, 2026-10-01, Tumbleweed clang 23, ASan and UBSan: three
  endless loops of the lazy walk found and fixed (5ca6ddd, 097451f);
  then 15 minutes, 2.29 million inputs, no crash, no timeout. The
  merged corpus has 1140 files.

- 2026-10-01, 756 assertions green in every row, -Wall -Wextra -O2:
  Tumbleweed gcc 16.2, clang 23.1 with libstdc++ and libc++, simdutf
  9.2.1. AlmaLinux 10.2 gcc 14.3, clang 21.1.8, no simdutf (not in
  EPEL 10). Alpine 3.24 musl, gcc 15.2, clang 22.1, simdutf. Nix gcc
  16.2, clang 21.1.8, simdutf 9.1.1 (needs simdutf.dev). clang warns
  about <ciso646> from the doctest header on AlmaLinux and Alpine.

- head_read, ns per head, 1M mixed heads, uid 1000, 3 of 4 cpus, not
  pinned, only process of the user, median of 41, x86_64: a loop over
  the bytes against 4 fixed widths with std::byteswap. gcc 14.2:
  11.23 against 10.16, noise 11.23/11.26. clang 21: 10.68 against
  10.11, noise 10.70/10.67. The fixed widths are in the tree.

- clang 21 with libstdc++ 14 and with libc++ 21 has no
  `std::float16_t`. gcc 14.2 has it. libc++ 21 has `std::flat_map`,
  libstdc++ 14 has not. Debian trixie with backports, in podman,
  2026-10-01.

- Security review of 2026-10-03, carsales encode, 7 alternating runs,
  medians: plain sum of string sizes 91.3 ns per car, ckd_add 92.5,
  the head before the review 91.6. The checked sum is in the tree.
