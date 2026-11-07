# API reference

mbedded-cbor is a header-only C++ library for CBOR (RFC 8949).

## Highlights

- **Fast.** `cbor::schema` reads one field of the carsales message of
  1000 cars in 11.6 ns. FlatBuffers with its Verifier takes 34.6 µs and
  Cap'n Proto 144 ns. Every library checks its input. AMD Ryzen 9 5950X,
  g++ 16.2 `-O2 -march=native`, median of 7 runs:
  [the run and its context](https://github.com/Asmod4n/mbedded-cbor/blob/main/bench/results/readme-2026-10-19-183938Z.json).
- **Standard CBOR.** Every message that the library writes is standard
  CBOR, and every other CBOR decoder reads it.
- **Safe on foreign input.** Bad input comes back as a `cbor::error`
  value. No read goes outside the message. Nesting depth, string length
  and element count are constants of the build.
- **JSONPath at compile time.** The compiler checks the path in
  `at_path<"$.user.id">`. A path also takes CBOR keys that are not text,
  goes through tags and reads typed arrays: [path](path/index.md).

## Example

```cpp
// bytes: a std::string with {"user": {"id": 42, "name": "ann"}}
auto const id = cbor::at_path<"$.user.id", int>(bytes);     // 42
auto const age = cbor::at_path<"$.user.age", int>(bytes);   // cbor::error::key_not_found

auto const doc = cbor::lazy::take_ownership(std::move(bytes));
auto const name = doc->at_path<"$.user.name", std::string_view>();  // "ann", a view into the message
```

## Installation

```sh
cmake -S mbedded-cbor -B build -DBUILD_TESTING=OFF
cmake --install build --prefix /usr/local
```

```cmake
find_package(mbedded-cbor 0.9 CONFIG REQUIRED)
target_link_libraries(app PRIVATE mbedded-cbor::mbedded-cbor)
```

Or with FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(mbedded-cbor
    GIT_REPOSITORY https://github.com/Asmod4n/mbedded-cbor
    GIT_TAG main)
FetchContent_MakeAvailable(mbedded-cbor)
```

It builds with g++ 16 in C++26 with `-freflection`, and with g++ 15,
clang 23 and the AppleClang of Xcode 16 and 26 in C++23. These compilers
need Boost.PFR 1.84 or later; CMake takes it from the system, else from
its own copy. On Windows it builds with clang-cl; the frontend of MSVC is
refused.

## Classes

| Group | Contents | Use it for |
|---|---|---|
| [lazy](lazy/index.md) | `cbor::lazy`: read a message in place, step by step | a message whose shape you learn while you read it |
| [path](path/index.md) | `cbor::at_path`: read a value by its JSONPath (RFC 9535), with CBOR keys that are not text, tags and typed arrays | one value whose path you know |
| [item](item/index.md) | `cbor::item`, `decode`, `encode`: a decoded tree and the encoder | a tree that lives on after the bytes are gone, and CBOR written item by item |
| [structs](structs/index.md) | `cbor::structs<T>`: any CBOR into a C++ struct and back | CBOR that any program wrote, read into the members of a struct |
| [schema](schema/index.md) | `cbor::schema<T>`: structs as Packed CBOR, fields at fixed offsets | messages that your own programs write and read |
| [ref](ref/index.md) | `cbor::ref<T>`, `cbor::owning<T>`: results that hold the bytes | a result that points into the message and stays valid |
| [tools](tools/index.md) | `item_size`, `sequence`, `sequence_reader`, `diagnostic_notation` | the end of an item, many items in a row, an item as text |
| [errors](errors/index.md) | `cbor::error`, `std::error_code`, the limits of a build | the reason why a call failed |

The names in `cbor::detail` are not part of the API. They can change in
any release.

## Speed

The carsales message of 1000 cars, read and written by each kind of
mbedded-cbor and by a library that does the same work. A number says how
many times faster mbedded-cbor is. A number under 1 says that the library
is faster. Every library checks its input, except where the row says so.

Read one field (`cars[500].engine.cc`):

| Kind | Library beside it | g++ 16 | clang 23 |
|---|---|---|---|
| lazy | tinycbor | 6.6× | 5.1× |
| at_path | jsoncons JSONPath | 50× | 47× |
| item | jsoncons `json` | 3.2× | 4.1× |
| structs | jsoncons `decode_cbor<T>` | 8.6× | 13× |
| schema | FlatBuffers with Verifier | 4700× | 2800× |
| schema, unverified | FlatBuffers, no Verifier | 0.40× | 0.27× |

Read the whole message:

| Kind | Library beside it | g++ 16 | clang 23 |
|---|---|---|---|
| item | jsoncons `json` | 3.2× | 4.0× |
| structs | jsoncons `decode_cbor<T>` | 8.7× | 13× |
| schema | FlatBuffers with Verifier | 1.3× | 1.5× |
| schema, unverified | FlatBuffers, no Verifier | 0.47× | 0.55× |

lazy and at_path read the values that you ask for and skip the rest.
For the whole message, use item or structs.

Write the message:

| Kind | Library beside it | g++ 16 | clang 23 |
|---|---|---|---|
| item | jsoncons `json` | 3.2× | 4.2× |
| structs | jsoncons `encode_cbor` | 4.6× | 7.9× |
| schema | FlatBuffers | 9.0× | 3.4× |

- schema reads Packed CBOR (155849 bytes). The other kinds and the
  libraries read the CBOR of `structs<T>::encode` (440957 bytes).
- item, structs and jsoncons decode the message before they read one
  field. jsoncons decodes the message before its path.
- jsoncons `decode_cbor<T>` copies each text into a `std::string`.
  structs gives views into the message.
- jsoncons writes no tags (422954 bytes).
- schema, unverified checks every bound, so no read leaves the input.
  It does not check the data (tags, heads). FlatBuffers without its
  Verifier checks no bounds.

Measured at commit cb28a52 in a VM with 4 cpus (Intel Xeon, Sapphire
Rapids), Debian sid, libstdc++ 16, `-O2 -march=native`, the median of 5
runs of 3 s. The noise of one run is up to 10 %. `bench/kinds-table.sh`
measures these tables on your machine, in the image of
`containers/debian-sid-bench.Containerfile`.

## More

- [Console](console.md): calls on a message of your own, in the browser.
- [GitHub](https://github.com/Asmod4n/mbedded-cbor): the source, the
  tests and the benchmarks.
