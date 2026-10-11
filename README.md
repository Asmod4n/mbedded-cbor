# mbedded-cbor

mbedded-cbor is a header-only C++ library for CBOR (RFC 8949). It reads
CBOR lazily: a read step goes only to the item it needs, and a string or
a byte string comes back as a view into the input, without a copy. It
reads values by JSONPath (RFC 9535), and a path can also name a CBOR key
that is not text. Databind and schema read the members of a struct by
reflection, so a struct needs no code of its own. The library reads and
writes Packed CBOR, shared references and typed arrays (RFC 8746). Each
read checks configurable limits. A function that reads input gives
`std::expected<T, cbor::error>` and throws no exception for bad input.

## Requirements

- The library needs C++23. The tests build with clang++-23 and with
  g++-16, with libstdc++ and with libc++.
- `cbor::databind` and `cbor::schema` need C++26 with `-freflection`.
  Only g++-16 has it. With clang, these two parts are not available,
  and the rest of the library works.
- The tests need doctest.

## Add it to a CMake project

The repository has no `install` step and no `find_package` support. Use
`add_subdirectory` or `FetchContent`, and link the target
`mbedded-cbor`:

```cmake
include(FetchContent)
FetchContent_Declare(mbedded-cbor GIT_REPOSITORY <url of this repository> GIT_TAG main)
FetchContent_MakeAvailable(mbedded-cbor)
target_link_libraries(app PRIVATE mbedded-cbor)
```

With `add_subdirectory(mbedded-cbor)` the last line is the same. The
target defines `_GLIBCXX_ASSERTIONS` and the libc++ fast hardening mode.
The tests are built only when mbedded-cbor is the top-level project.
For databind and schema, compile your target with `-std=c++26
-freflection`.

## Quick start

```cpp
#include <cbor/cbor.hpp>

// {"user": {"id": 42, "name": "ann"}}
std::string const bytes = "\xa1\x64user\xa2\x62id\x18\x2a\x64name\x63" "ann"s;

auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
// *id == 42

auto const doc = cbor::lazy::from(bytes);
auto const name = doc->at("user")
                      .and_then([](cbor::lazy const &u) { return u.at("name"); })
                      .and_then([](cbor::lazy const &n) { return n.get<std::string_view>(); });
// **name == "ann", a view into the bytes that holds their owner

auto const missing = cbor::at_path<"$.user.age", std::int64_t>(bytes);
// missing.error() == cbor::error::key_not_found
```

This example is the test `doc readme: quick start` in
`test/doc_readme.cpp`.

## Documents

- [doc/basics.md](doc/basics.md): errors, limits, memory, nesting depth,
  threads and owners, which every API shares.
- [doc/runtime.md](doc/runtime.md): `cbor::lazy`, decode, encode,
  sequences, diagnostic notation and `item_size`.
- [doc/path.md](doc/path.md): JSONPath on CBOR.
- [doc/databind.md](doc/databind.md): a C++ value to CBOR and back.
- [doc/schema.md](doc/schema.md): reads by a schema that a struct gives.
- [doc/comparison.md](doc/comparison.md): six tasks with mbedded-cbor and
  other libraries.
- [doc/known-limits.md](doc/known-limits.md): what the library does not
  do, and the open faults.

## Tests and bench

`test/run.sh fast` builds and runs the tests with g++-16 (release) and
clang++-23 (AddressSanitizer). `test/run.sh full` adds three builds:
g++-16 with AddressSanitizer, clang++-23 release and clang++-23 with
libc++. The builds run in podman, in the image `mbedded-cbor-tw`, and the
build directories are in `$CBOR_BUILD_ROOT`.

`bench/run-desktop.sh <commit-a> <commit-b>` measures two commits with
g++ and clang++ and writes one result file to `bench/results/`.
`bench/README.md` states the arms, the inputs and the conditions of a run.

## License

Apache License 2.0. See [LICENSE](LICENSE).
