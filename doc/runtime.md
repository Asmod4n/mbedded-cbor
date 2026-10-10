# The run-time API

This document states the functions that read and write CBOR with no
schema: `cbor::lazy`, the decode and encode with a binding,
`cbor::sequence`, `cbor::diagnostic_notation` and `cbor::item_size`.
Errors, limits, memory, threads and ownership are in `doc/basics.md`.
Paths are in `doc/path.md`.

Each claim in this document has a test in `test/doc_runtime.cpp`. The test
names start with `doc runtime:`.

## Contents

1. lazy::from
2. A step into a map or an array
3. elements and entries
4. A search between two iterators
5. get
6. decode and lazy_decode
7. encode
8. cbor::sequence
9. diagnostic_notation
10. item_size
11. Tags 28 and 29
12. Tag 55799

Every function is in the namespace `cbor`. The examples write the bytes
as C++ string literals with the suffix `s` for `std::string` and `sv` for
`std::string_view`.

## 1. lazy::from

A `cbor::lazy` is a position in one top-level item. `lazy::from` makes the
lazy of the top-level item. It reads no head and checks only the limit
`input_bytes`. A step reads the heads it needs, when it is called.

| Form | The bytes |
|---|---|
| `lazy::from(std::string&&)` | moved into a new owner, no copy |
| `lazy::from(std::string_view)` | copied into a new owner |
| `lazy::from(std::shared_ptr<std::string const>)` | the shared pointer is the owner |
| `lazy::from(std::shared_ptr<void const> owner, std::string_view)` | `owner` holds them |

```cpp
auto const a = cbor::lazy::from("\x82\x01\x02"s);           // moved
auto const b = cbor::lazy::from("\x82\x01\x02"sv);          // copied
auto const held = std::make_shared<std::string const>("\x82\x01\x02"s);
auto const c = cbor::lazy::from(held);
auto const d = cbor::lazy::from(held, *held);               // owner and bytes
```

Each form gives `std::expected<lazy, error>`. A null pointer or an empty
owner throws `std::logic_error`. A `std::string` lvalue goes to the form
with `std::string_view` and is copied. The form with an owner does not take
a temporary `std::string`.

`lazy::from` does not check that the bytes are one complete item. It
accepts `82 01`, and a read of the missing element gives
`too_little_data`. It accepts bytes after the item, and no step reads
them.

## 2. A step into a map or an array

Each step gives a `std::expected`, and `and_then` gives it to the next
step. An error at any step reaches the end of the chain.

| Call | Result |
|---|---|
| `at(std::string_view key)` | `std::expected<lazy, error>`, the value of the text key |
| `at(cbor::key{n})` | `std::expected<lazy, error>`, the value of the integer key n |
| `at(std::size_t index)` | `std::expected<lazy, error>`, the element of the array |
| `find(key)` | `std::expected<lazy_entries::iterator, error>`, the first pair of the key, or the end |
| `contains(key)` | `std::expected<bool, error>` |
| `count(key)` | `std::expected<std::size_t, error>`, every pair with the key |
| `size()` | `std::expected<std::uint64_t, error>`, the elements of an array or the pairs of a map |
| `empty()` | `std::expected<bool, error>` |

`find`, `contains` and `count` take a `std::string_view` or a
`std::int64_t`.

```cpp
auto const doc = *cbor::lazy::from(/* {"cars": [{"hp": 5}, {"hp": 7}], 7: "seven"} */);
doc.at("cars")
    .and_then([](cbor::lazy const &c) { return c.at(1); })
    .and_then([](cbor::lazy const &c) { return c.at("hp"); })
    .and_then([](cbor::lazy const &h) { return h.get<std::int64_t>(); });  // 7
doc.at(cbor::key{7});                     // the lazy of "seven"
doc.contains("cars");                     // true
doc.count(std::int64_t{7});               // 1
doc.size();                               // 2
doc.at("cars")->at(5).error();            // index_out_of_bounds
doc.at("bus").error();                    // key_not_found
doc.at(0).error();                        // not_indexable, a map has no index
```

A key is a text key or an integer key. A text key does not match a byte
string with the same bytes. A key under tag 28 or tag 29 matches the key
that it marks or names.

A map with a key twice is no error. `at`, `find` and `contains` give the
first pair. `count` counts every pair, as `std::multimap::count` does.

A step reads the heads from the start of its map or array to the target.
A step also reads the head of its target. A target that is not in the
input gives `too_little_data` at the step, not at a later read. A step
reads no byte after the head of the target.

## 3. elements and entries

`elements()` gives the elements of an array. `entries()` gives the pairs of
a map, in the order of the bytes. Each is a view: a forward range, a sized
range and a borrowed range. The standard algorithms and views take them.

| Call | Element of the view |
|---|---|
| `elements()` | `std::expected<lazy, error>` |
| `entries()` | `std::expected<std::pair<lazy, lazy>, error>` |

```cpp
auto const l = *cbor::lazy::from(/* [1, "a", [2]] */);
for (auto const e : *l.elements())
    e->get<std::int64_t>();               // 1, then incorrect_type, then incorrect_type
l.elements()->size();                     // 3
auto const m = *cbor::lazy::from(/* {"a": 1, 2: "b"} */);
for (auto const p : *m.entries())
    p->first;                             // the lazy of "a", then of 2
std::ranges::distance(*m.entries() | std::views::drop(1));  // 1
```

`elements()` of a map and `entries()` of an array give `not_indexable`.
An array or a map of indefinite length gives `indefinite_length`.

An element that cannot be read gives its error, and the iterator then
stands at the end. A view gives at most `size()` values.

An iterator holds the top-level item. It stays valid after the lazy and the
view that gave it end. An iterator belongs to one thread (doc/basics.md).

## 4. A search between two iterators

| Call | Result |
|---|---|
| `lazy::find(first, last, key)` | the first pair of the key in `[first, last)`, or `last` |
| `lazy::equal_range(first, last, key)` | `std::expected<std::ranges::subrange<lazy_entries::iterator>, error>` |

`last` is an iterator of the same map or `std::default_sentinel`. Both are
static members and take a `std::string_view` or a `std::int64_t` key.

`find` reads only `[first, last)`. A key before `first` is not found.

`equal_range` is for a map with its keys sorted by their encoded bytes
(RFC 8949 4.2.1). It stops at the first key that sorts after the wanted
key. A missing key gives an empty range at the place where the key would
be. A map that is not sorted gives a result with no meaning.

```cpp
auto const m = *cbor::lazy::from(/* {"b": 2, "d": 4, "f": 6, "h": 8} */);
auto const e = *m.entries();
auto const h = *m.find("h");
cbor::lazy::find(e.begin(), h, "d");             // the pair "d": 4
cbor::lazy::find(h, std::default_sentinel, "d"); // the end
cbor::lazy::equal_range(e.begin(), h, "f");      // one pair, "f": 6
cbor::lazy::equal_range(e.begin(), h, "e");      // empty, at "f"
```

Two iterators of two different maps throw `std::logic_error`.

## 5. get

`get<T>()` reads the item at the lazy as T.

| T | Item | Result |
|---|---|---|
| an integer type, not `bool` | an integer, or a bignum (tag 2, 3) that fits T | T |
| `double` | a float of 16, 32 or 64 bits, or an integer from -2^53 to 2^53 | `double` |
| `bool` | `false`, `true` | `bool` |
| `std::nullptr_t` | `null` | `nullptr` |
| `cbor::simple_value` | a simple value | the value |
| `std::string_view` | a text string | `owning_ref<std::string_view>` |
| `std::span<std::byte const>` | a byte string | `owning_ref<std::span<std::byte const>>` |
| `cbor::typed_array` | a typed array (RFC 8746) | `owning_ref<cbor::typed_array>` |

Another kind of item gives `incorrect_type`. A number that does not fit T
gives `number_out_of_range`. A view points into the bytes and holds the
owner (doc/basics.md).

```cpp
cbor::lazy::from("\x18\x2a"s)->get<std::int64_t>();        // 42
cbor::lazy::from("\x19\x01\x00"s)->get<std::uint8_t>();    // number_out_of_range
cbor::lazy::from("\xf9\x3e\x00"s)->get<double>();          // 1.5
cbor::lazy::from("\x18\x2a"s)->get<double>();              // 42.0
cbor::lazy::from("\xf5"s)->get<bool>();                    // true
cbor::lazy::from("\x63""abc"s)->get<std::string_view>();   // owning_ref, "abc"
cbor::lazy::from("\x63""abc"s)->get<bool>();               // incorrect_type
```

`get` follows a tag 29 to the item that it names, and passes a tag 28.

## 6. decode and lazy_decode

`lazy::decode()` gives `std::expected<std::shared_ptr<cbor::item const>,
error>`. It reads the whole item at the lazy and checks every head. A
`cbor::item` holds the major type, the additional information, the
argument and the content of one item. A string in it is a view into the
bytes.

```cpp
auto const i = cbor::lazy::from("\x82\x01\x02"s)->decode();
(*i)->major_type;   // major_type::array
(*i)->argument;     // 2
```

`lazy_decode(binding, l)` reads the whole item at `l` into the values of
your binding. A binding is your type with a member type `value`. The
library calls the binding for each item: `decode.hpp` lists the calls. The
binding is an lvalue.

```cpp
lazy_decode(binding, *cbor::lazy::from("\xa1\x61""a\x82\x01\x02"s));  // {"a": [1, 2]}
lazy_decode(binding, *cbor::lazy::from("\x82\x01"s));                 // too_little_data
```

Both read every head of the item, so every limit counts each head. Neither
reads a byte after the item.

## 7. encode

`encode(binding, target, value)` writes `value` as CBOR. It gives
`std::expected<void, error>`. The binding answers what kind each value is.

`target` is one of:

- a container of bytes, for example `std::string` or
  `std::vector<std::byte>`: the encode appends to it;
- a fixed span of bytes: the encode writes into it and gives
  `no_buffer_space` when it does not fit;
- a writer of your own with `allocate`, `append` and `done`. An error of
  the writer comes back as a `cbor::error` (doc/basics.md).

```cpp
std::string out;
cbor::encode(binding, out, A(1, 2));          // out holds 82 01 02
```

`encode<cbor::sharedrefs::on>(...)` writes a value that the binding gives
more than once as one tag 28 and a tag 29 for each further use. The default
`sharedrefs::off` writes each use in full, and a cycle gives
`cyclic_data_structure`.

## 8. cbor::sequence

`cbor::sequence` reads a CBOR Sequence (RFC 8742): top-level items one
after the other, with nothing between them. Its iterator gives
`std::expected<std::string_view, error>`, the bytes of one item.

```cpp
std::string const bytes = "\x01\x61""a\x82\x01\x02"s;
for (auto const e : cbor::sequence{bytes})
    *e;   // "\x01", then "\x61a", then "\x82\x01\x02"
```

- A last item that is not complete gives `too_little_data`. The iterator
  keeps the bytes that are left in its member `encoded`, so a reader of a
  stream can read them again with more bytes.
- An item that is not well-formed gives its error, and the sequence ends
  there.
- The sequence does not take a temporary string.

Each element is a top-level item of its own. A tag 29 in one element does
not reach a tag 28 in another. `lazy::from(owner, *e)` reads an element
with the owner of the whole sequence and copies nothing.

## 9. diagnostic_notation

`diagnostic_notation(encoded)` gives the item in the diagnostic notation
of RFC 8949 section 8, as `std::expected<std::string, error>`. A head that
is longer than the preferred one carries its encoding indicator, for
example `1_1`.

```cpp
cbor::diagnostic_notation("\x82\x01\x02"sv);  // "[1, 2]"
cbor::diagnostic_notation("\x19\x00\x01"sv);  // "1_1"
cbor::diagnostic_notation("\x82\x01"sv);      // too_little_data
```

It reads every head of the first item.

## 10. item_size

`item_size(encoded)` gives the size in bytes of the first top-level item,
as `std::expected<std::size_t, error>`. It checks that the item is
well-formed and allocates nothing. It keeps a count and no stack, so the
nesting depth does not limit it.

```cpp
cbor::item_size("\x01\x02"sv);    // 1
cbor::item_size("\x82\x01"sv);    // too_little_data
```

## 11. Tags 28 and 29

Tag 28 marks a value as shared. Tag 29 with the index n stands for the
value of the n-th tag 28, counted from 0 in the order of the bytes.

- A step, `get`, `elements`, `entries` and a path read through a tag 28
  to its content, and through a tag 29 to the value it names.
- A tag 29 must come after the tag 28 that it names. A tag 29 that names
  no tag 28 before it gives `sharedref_index_not_marked`.
- A tag 29 inside the tag 28 that it names is a cycle. A binding that does
  not take a cycle gets `sharedref_not_complete`.
- `diagnostic_notation` writes the tags as they are.

```cpp
auto const l = *cbor::lazy::from("\x82\xd8\x1c\x01\xd8\x1d\x00"s);   // [28(1), 29(0)]
l.at(1)->get<std::int64_t>();                                         // 1
cbor::diagnostic_notation("\x82\xd8\x1c\x01\xd8\x1d\x00"sv);          // "[28(1), 29(0)]"
lazy_decode(binding, *cbor::lazy::from("\x82\xd8\x1d\x00\xd8\x1c\x05"s)); // sharedref_index_not_marked
```

The first tag 29 that a top-level item reads finds the tags 28 before it,
once, and keeps their positions. A later tag 29 reuses them.

## 12. Tag 55799

Tag 55799 (RFC 8949 3.4.6) marks bytes as CBOR. `lazy::from`, `decode`,
`lazy_decode` and a path read one leading tag 55799 as its content, in
every width of the argument. A second tag 55799, and a tag 55799 inside an
item, stay tags.

```cpp
cbor::lazy::from("\xd9\xd9\xf7\x82\x01\x02"s)->at(1)->get<std::int64_t>();  // 2
lazy_decode(binding, *cbor::lazy::from("\x81\xd9\xd9\xf7\x01"s));            // [55799(1)]
```

The encoder writes a tag 55799 that the binding gives as `d9 d9 f7`.
