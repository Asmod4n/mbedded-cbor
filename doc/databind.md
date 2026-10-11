# Databind

`cbor::databind<T>` writes a C++ value as one CBOR data item and reads one
CBOR data item into a C++ value. The type `T` gives the form of the item.
The compiler reads the members of a struct by reflection, so a struct
needs no code of its own.

Each claim in this document has a test in `test/doc_databind.cpp`. The test
names start with `doc databind:`. doc/basics.md describes what every API
shares: errors, wrong use, limits, memory, threads and owners.

## Contents

1. Encode and decode
2. The owner of the bytes
3. Types
4. Structs and keys
5. Variants
6. Shared references
7. Errors
8. Limits and memory
9. Compilers

## 1. Encode and decode

| Call | Result |
|---|---|
| `databind<T>::encode(value)` | `std::expected<std::string, error>`, the bytes |
| `databind<T>::encode(value, target)` | `std::expected<std::size_t, error>`, the count of bytes appended to `target` |
| `databind<T>::decode(...)` | `std::expected<owning_ref<T>, error>` |

```cpp
struct person {
    std::string name;
    std::uint8_t age;
};

auto const bytes = cbor::databind<person>::encode(person{"ann", 30});
// a2 64 "name" 63 "ann" 63 "age" 18 1e
auto const back = cbor::databind<person>::decode(*bytes);
(*back)->name;   // "ann"
```

`encode` with a target appends. The target is a `std::string`, a
container of `char`, a `std::span<char>` or a writer (doc/runtime.md).

```cpp
std::string out = "\x82";
cbor::databind<std::uint8_t>::encode(1, out);   // 1
cbor::databind<std::uint8_t>::encode(2, out);   // 1; out is 82 01 02
```

`decode` reads exactly one item. Bytes after the item give
`error::syntax_error`.

## 2. The owner of the bytes

A result can hold views into the bytes: `std::string_view` and
`std::span<std::byte const>`. So `decode` always returns an
`owning_ref<T>`, which holds the owner of the bytes. A view in the result
stays valid while the `owning_ref` lives.

| Call | Owner |
|---|---|
| `decode(std::string)` with an rvalue | the library moves the string into a new `std::shared_ptr<std::string const>` |
| `decode(std::string_view)` | the library copies the bytes into a new `std::shared_ptr<std::string const>` |
| `decode(owner, encoded)` | the caller: `owner` holds the bytes of `encoded` |
| `decode(owner, encoded, target)` | the caller, and `decode` reads into `target` |

A `std::string` lvalue and a `char const *` take the copying form.

```cpp
std::string const bytes = "\x63" "abc";
cbor::databind<std::string_view>::decode(bytes);                 // a copy
std::string moved = /* a text of 40 bytes */;
cbor::databind<std::string_view>::decode(std::move(moved));      // no copy
auto const owner = std::make_shared<std::string const>(bytes);
cbor::databind<std::string_view>::decode(owner, *owner);         // views into *owner
```

An empty owner throws `std::logic_error`. A temporary `std::string` beside
an owner does not compile (doc/basics.md, section 8).

## 3. Types

| C++ type | CBOR item |
|---|---|
| `bool` | `true`, `false` |
| `std::nullptr_t` | `null` |
| `cbor::simple_value` | a simple value |
| an integer of 8 to 64 bits | major type 0 or 1 |
| `__int128`, `unsigned __int128` | major type 0 or 1, or a bignum (tag 2 or 3) |
| an enum with a fixed underlying type | the integer of its underlying type |
| `float`, `double` | the shortest float that holds the value exactly |
| `std::string`, `std::pmr::string`, `std::string_view` | a text string |
| `std::vector<std::byte>`, `std::array<std::byte, N>`, `std::span<std::byte const>` | a byte string |
| `std::optional<E>` | the item of E, or `null` when empty |
| `std::variant<E...>` | the item of the alternative it holds |
| `std::tuple<E...>`, `std::array<E, N>` | an array of exactly that count |
| `std::map`, `std::unordered_map` and their `std::pmr` forms | a map |
| `std::vector`, `std::deque`, `std::list`, `std::set` and other containers | an array |
| `cbor::tagged<N, E>` | tag N on the item of E |
| an aggregate struct | a map, section 4 |

`std::string_view` and `std::span<std::byte const>` point into the bytes of
the owner. Every other type holds its own copy.

```cpp
cbor::databind<std::set<std::uint8_t>>::encode({3, 1});                  // 82 01 03
cbor::databind<std::array<std::byte, 2>>::encode({std::byte{1}, std::byte{2}});   // 42 01 02
cbor::databind<cbor::tagged<1, std::int64_t>>::encode({1700000000});     // c1 1a 65 53 f1 00
```

An enum with no fixed underlying type does not compile in `decode`,
because some values of its underlying type are not values of the enum.

A `float` takes a binary16, binary32 or binary64 item. A binary64 item
that a `float` cannot hold exactly gives `error::number_out_of_range`.
An integer that its C++ type cannot hold gives
`error::number_out_of_range`.

## 4. Structs and keys

A struct is a map. Each public data member is one entry. The key is the
name of the member as a text string. The entries follow the order of the
members.

| Annotation | Effect |
|---|---|
| `[[=cbor::key("x")]]` on a member | the key is the text `x` |
| `[[=cbor::skip{}]]` on a member | the member has no entry |
| `[[=cbor::allowlist{}]]` on the struct | only the members with `[[=cbor::allow{}]]` have an entry |
| `static constexpr std::array<std::int64_t, N> keys` | the keys are these integers, one for each member |

```cpp
struct header {
    [[=cbor::key("content-type")]] std::string content_type;
    [[=cbor::skip{}]] std::string cache;
};
// header{"text/plain", "x"}: a1 6c "content-type" 6a "text/plain"

struct senml {          // RFC 8428 6: n is 0, v is 2
    std::string n;
    double v;
    static constexpr std::array<std::int64_t, 2> keys{0, 2};
};
// senml{"t", 1.5}: a2 00 61 "t" 02 f9 3e 00
```

`encode` leaves out a member of type `std::optional` that is empty.
`decode` gives an empty optional when its key is not in the map.

`decode` reads the entries of a map in any order:

- a key that no member has: the entry is skipped;
- a key that comes twice: the first entry gives the value;
- a missing key of a member that is not optional: `error::key_not_found`;
- a key that is an array, a map or a tag: `error::unsupported_value`.

`databind` writes a tag on a struct only through `cbor::tagged<N, E>`.
A struct that carries `[[=cbor::tag(n)]]` does not compile in
`databind`. Only the schema form reads that annotation (doc/schema.md).

```cpp
struct person { std::string name; std::int64_t age; };
cbor::databind<cbor::tagged<1800, person>>::encode({{"ann", 30}});
// 1800({"name": "ann", "age": 30})
```

## 5. Variants

`decode` gives a `std::variant` the first alternative that accepts the head
of the item. An integer alternative accepts an integer that it can hold.

```cpp
using id = std::variant<std::uint8_t, std::int64_t, std::string>;
decode 18 2a      // index 0, the uint8_t 42
decode 20         // index 1, the int64_t -1
decode 19 01 00   // index 1, the int64_t 256
decode 61 78      // index 2, the string "x"
decode f5         // error::incorrect_type
```

`encode` writes the alternative that the variant holds.

## 6. Shared references

`decode` reads tag 28 and tag 29 of the value-sharing registration of
the IANA tag registry. A tag 29 reads as the item that it names.

```cpp
cbor::databind<std::vector<std::string>>::decode("\x82\xd8\x1c\x61x\xd8\x1d\x00"s);   // {"x", "x"}
```

`encode` writes no shared reference.

## 7. Errors

| Input | Error |
|---|---|
| an integer that the C++ type cannot hold | `number_out_of_range` |
| a binary64 that a `float` cannot hold exactly | `number_out_of_range` |
| an item of another major type | `incorrect_type` |
| an array whose count differs from a `std::tuple` or `std::array` | `incorrect_type` |
| a variant with no alternative that accepts the item | `incorrect_type` |
| a missing key of a member that is not optional | `key_not_found` |
| bytes after the item | `syntax_error` |
| the input ends inside the item | `too_little_data` |
| a refused allocation | `not_enough_memory` |

doc/basics.md, section 3, lists every error value.

## 8. Limits and memory

`decode` applies the limits in force: the count of input bytes and the
nesting depth (doc/basics.md, sections 4 and 5).

`decode` does not reserve memory for a count that the input states. Each
element is made when it is read.

The form with a target reads into the containers of the target. A target
of `std::pmr` containers sets the memory resource of every element.
When the resource refuses, `decode` returns `error::not_enough_memory`.

```cpp
using texts = std::pmr::vector<std::pmr::string>;
std::array<std::byte, 64> small{};
std::pmr::monotonic_buffer_resource budget(small.data(), small.size(),
                                           std::pmr::null_memory_resource());
cbor::databind<texts>::decode(owner, *owner, texts(&budget));   // three texts of 40 bytes: not_enough_memory
```

doc/basics.md, section 6, describes the memory of every API, and section
7 the threads.

## 9. Compilers

`databind` needs static reflection and annotations of C++26. g++ 16 with
`-freflection` builds it. clang has no reflection, so a clang build has no
`cbor::databind`.
