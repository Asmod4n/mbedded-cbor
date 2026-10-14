# mbedded-cbor

A header-only C++ library for CBOR (RFC 8949). The steps below go from a
first read to input from the network.

## 1. Add the library

Install it, then use `find_package`:

```sh
cmake -S mbedded-cbor -B build -DBUILD_TESTING=OFF
cmake --install build --prefix /usr/local
```

```cmake
find_package(mbedded-cbor 0.9 CONFIG REQUIRED)
target_link_libraries(app PRIVATE mbedded-cbor::mbedded-cbor)

# packed and structs: C++26 reflection, g++ 16
target_compile_features(app PRIVATE cxx_std_26)
target_compile_options(app PRIVATE -freflection)
```

Or use `FetchContent` without an install:

```cmake
include(FetchContent)
FetchContent_Declare(mbedded-cbor
    GIT_REPOSITORY https://github.com/Asmod4n/mbedded-cbor
    GIT_TAG main)
FetchContent_MakeAvailable(mbedded-cbor)
```

The target also defines `_GLIBCXX_ASSERTIONS` and
`_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST`. These turn on the
checks of libstdc++ and libc++. `pkg-config --cflags mbedded-cbor` gives
the same flags.

## 2. Read a message

`cbor::lazy` reads the bytes in place and builds no tree. Each call
reads only up to the item that it looks for. No reflection needed.
`lazy::copy(bytes)` copies the bytes once. `std::move` or a
`std::shared_ptr` avoids the copy
([doc/advanced/basics.md](doc/advanced/basics.md#8-owners)).

```cpp
// bytes holds the CBOR of {"sensor": "t1", "value": 21.5, "samples": [3, 4, 5]}
auto const doc = cbor::lazy::copy(bytes).value();

bool const has_samples = doc.contains("samples").value();  // true
std::uint64_t const pairs = doc.size().value();            // 3

int sum = 0;
if (auto const samples = doc.at("samples"))
    for (auto const &x : samples->elements().value())
        if (x)
            sum += x->get<int>().value_or(0);  // sum is 12
```

## 3. Read one value by its path

The read side needs only C++23. It works with g++ 16 and with clang 23.

```cpp
// bytes is a std::string with the CBOR of {"user": {"id": 42, "name": "ann"}}
auto const doc = cbor::lazy::copy(bytes).value();

auto const id = doc.at_path<"$.user.id", std::int64_t>();         // *id == 42
auto const name = doc.at_path<"$.user.name", std::string_view>();  // **name == "ann"
auto const age = doc.at_path<"$.user.age", int>();
// age.error() == cbor::error::key_not_found
```

`id` is a `std::expected<std::int64_t, cbor::error>`. `name` holds a
`cbor::owning_ref<std::string_view>`: a view into the bytes, with no
copy. The owning_ref keeps the bytes alive, so the view stays valid.

## 4. Handle bad input

```cpp
std::string const cut = bytes.substr(0, bytes.size() - 1);  // the last byte is missing

auto const doc = cbor::lazy::copy(cut).value();
auto const name = doc.at_path<"$.user.name", std::string_view>();
// name.error() == cbor::error::too_little_data

std::string const why = std::error_code(name.error()).message();  // "too little data"
```

A read stops at the item that it looks for: `$.user.id` still reads 42 from
`cut`, because the missing byte comes after it. A read never throws for bad
input. Every error is a `cbor::error`, and
it converts to `std::error_code`.

## 5. Set limits for input from the network

```cpp
cbor::limits = {.nesting_depth = 16, .string_length = 4096, .container_elements = 100};

// bytes holds a CBOR array of 101 elements
auto const n = cbor::lazy::copy(bytes).value().size();
// n.error() == cbor::error::container_elements_exceeded
```

The limits hold for every later call in every thread. A field that you
leave out gets its default: nesting depth 128, no limit on string
length, element count and input size. A count in the input reserves no
memory.

## 6. Read a stream of items

A CBOR sequence (RFC 8742) is items one after the other, for example a
log.

```cpp
// log is a std::shared_ptr<std::string const> with three CBOR items: 1, "a", [1, 2]
for (auto const &item : cbor::sequence{log})
    if (item)
        std::println("{}", cbor::diagnostic_notation(*item).value());
```

```
1
"a"
[1, 2]
```

Each item is a `std::expected<std::string_view, cbor::error>`: the
bytes of one item. A last item that is not complete gives
`cbor::error::too_little_data`. The iterator then keeps the bytes that
are left in its member `encoded`, so a reader of a stream can read them
again with more bytes.

## 7. Packed: fields at fixed offsets

A CBOR message carries its keys and its structure in every message. To
read one field, a reader walks over the items before it. `cbor::packed`
removes that walk. It writes a struct as Packed CBOR
(draft-ietf-cbor-packed-19): the struct gives the layout, so a field of
fixed size sits at an offset that the compiler knows, and a read goes
to that offset directly. FlatBuffers and Cap'n Proto work the same way.
The result is still CBOR, and a decoder of Packed CBOR reads it as a
map. Each struct
needs its own tag number. Needs reflection.

```cpp
struct [[=cbor::tag(1900)]] series {
    std::string unit;
    std::vector<double> values;
};

std::string const bytes = cbor::packed<series>::encode({"m", {1.5, 2.5, 3.5}}).value();

auto const second = cbor::packed<series>::at_path<"$.values[1]">(bytes);
// *second == 2.5, read at its offset

auto const root = cbor::packed<series>::path(bytes).value();
auto const values = root.view<"$.values">();  // *values: a random-access range of double
double const sum = std::ranges::fold_left(*values, 0.0, std::plus{});  // 7.5
```

`values` is stored as a typed array (RFC 8746): tag 86, float64
little-endian, followed by one byte string with all elements. Every
range of integers or floats is stored this way. A typed array is fast
for these reasons:

- The elements have no head each. Element `i` sits at offset `i * 8`
  from the start of the byte string.
- On a little-endian machine, the encode copies the whole range as one
  block of bytes.
- The read checks the tag and the length once. After that, a read of an
  element checks only the index against the size.
- `view` gives the elements in place, with no copy.

## 8. Structs: any CBOR into a struct

`cbor::structs` puts any CBOR into the members of a C++ struct or
class, and writes it back as deterministic CBOR (RFC 8949 4.2.1): the
shortest form of every number, and the keys in a fixed order. Needs
reflection.

```cpp
struct reading {
    std::string sensor;
    double value;
    std::vector<std::uint16_t> samples;
};

std::string const bytes = cbor::structs<reading>::encode({"t1", 21.5, {3, 4, 5}}).value();

std::string const text = cbor::diagnostic_notation(bytes).value();
// text is {"value": 21.5, "sensor": "t1", "samples": [3, 4, 5]}
std::size_t const size = bytes.size();
// size == 32; the same data as JSON with no spaces has 46 bytes
```

## Next

- [doc/basics.md](doc/basics.md): errors, limits, memory, threads.
- [doc/path.md](doc/path.md): JSONPath (RFC 9535) on CBOR. `match()`
  and `search()` are not supported.
- [doc/structs.md](doc/structs.md) and
  [doc/packed.md](doc/packed.md): every type and annotation.

## Tests

The tests run the examples of RFC 8949 Appendix A
([hildjj/test-vectors](https://github.com/hildjj/test-vectors)), the
test vectors of the IETF CBOR working group
([cbor-wg/cbor-test-vectors](https://github.com/cbor-wg/cbor-test-vectors))
and the
[JSONPath Compliance Test Suite](https://github.com/jsonpath-standard/jsonpath-compliance-test-suite).
They run with g++ 16 and clang 23, also with AddressSanitizer and
UndefinedBehaviorSanitizer. Fuzz targets cover decode, encode, encoder,
item_size, lazy, path and packed (CMake option `CBOR_FUZZ`).

## License

Apache License 2.0. See [LICENSE](LICENSE).
