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

## More

- [Console](console.md): calls on a message of your own, in the browser.
- [GitHub](https://github.com/Asmod4n/mbedded-cbor): the source, the
  tests and the benchmarks.
