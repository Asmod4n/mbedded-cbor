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
- `cbor::structs` and `cbor::packed` need C++26 with `-freflection`.
  Only g++-16 has it. With clang, these two parts are not available,
  and the rest of the library works.
- The tests need doctest.

## Add it to a CMake project

Install the library, then find it with `find_package` and link the
target `mbedded-cbor::mbedded-cbor`:

```sh
cmake -S mbedded-cbor -B build -DBUILD_TESTING=OFF
cmake --install build --prefix /usr/local
```

```cmake
find_package(mbedded-cbor 0.9 CONFIG REQUIRED)
target_link_libraries(app PRIVATE mbedded-cbor::mbedded-cbor)
```

The install also writes `share/pkgconfig/mbedded-cbor.pc`, so
`pkg-config --cflags mbedded-cbor` gives the include directory and the
definitions.

Without an install, use `add_subdirectory` or `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(mbedded-cbor GIT_REPOSITORY <url of this repository> GIT_TAG main)
FetchContent_MakeAvailable(mbedded-cbor)
target_link_libraries(app PRIVATE mbedded-cbor::mbedded-cbor)
```

With `add_subdirectory` the last line is the same. A path outside the
source tree of the project needs the second argument, the binary
directory: `add_subdirectory(/path/to/mbedded-cbor mbedded-cbor)`. The
target defines `_GLIBCXX_ASSERTIONS` and the libc++ fast hardening mode.
The tests and the install rules are active only when mbedded-cbor is
the top-level project; `-DCBOR_INSTALL=ON` turns the install rules on
for a project that installs a target which links mbedded-cbor.
For structs and schema, compile your target with `-std=c++26
-freflection`.

## Quick start

`<cbor/cbor.hpp>` includes every part of the library: `cbor::lazy`,
`cbor::at_path`, `cbor::limits`, and, with `-freflection`,
`cbor::structs` and `cbor::packed`. A program needs no other include of
the library.

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

### Write a struct and read one value by a path

This example needs `-std=c++26 -freflection`.

```cpp
#include <cbor/cbor.hpp>

struct inner { std::int64_t b; };
struct outer { inner a; };

auto const bytes = cbor::structs<outer>::encode(outer{{5}});
// a1 61 "a" a1 61 "b" 05, which is {"a": {"b": 5}}
auto const b = cbor::at_path<"$.a.b", std::int64_t>(*bytes);
// *b == 5
```

This example is the test `doc readme: write a struct and read by a path`
in `test/doc_readme.cpp`.

### Set a limit

```cpp
#include <cbor/cbor.hpp>

cbor::limits.container_elements = 100;
// an array or a map with more than 100 elements now gives
// cbor::error::container_elements_exceeded
```

`cbor::limits` is one object for the whole process. doc/basics.md
section 4 describes every limit. This example is the test
`doc readme: set a limit` in `test/doc_readme.cpp`.

## Documents

- [doc/basics.md](doc/basics.md): errors, limits, memory, nesting depth,
  threads and owners, which every API shares.
- [doc/runtime.md](doc/runtime.md): `cbor::lazy`, decode, encode,
  sequences, diagnostic notation and `item_size`.
- [doc/path.md](doc/path.md): JSONPath on CBOR.
- [doc/structs.md](doc/structs.md): a C++ value to CBOR and back.
- [doc/packed.md](doc/packed.md): reads by a schema that a struct gives.
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
