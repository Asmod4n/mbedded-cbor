# Run-time API in detail

This page adds the details to [../runtime.md](../runtime.md). Every
function is in the namespace `cbor`. A comment shows the CBOR in `bytes`
as text. Where the bytes themselves matter, a comment gives them in hex.

## 1. lazy::from

A `cbor::lazy` is a position in one top-level item. `lazy::from` makes
the lazy of the top-level item.

| Form | The bytes |
|---|---|
| `lazy::from(std::string&&)` | moved into a new owner, no copy |
| `lazy::from(std::string_view)` | copied into a new owner |
| `lazy::from(std::shared_ptr<std::string const>)` | the shared pointer is the owner |
| `lazy::from(std::shared_ptr<void const> owner, std::string_view)` | `owner` holds them |

```cpp
auto const a = cbor::lazy::from(std::move(bytes));         // moved
auto const b = cbor::lazy::from(std::string_view{bytes});  // copied
auto const held = std::make_shared<std::string const>(bytes);
auto const c = cbor::lazy::from(held);
auto const d = cbor::lazy::from(held, *held);              // owner and bytes
```

Each form gives `std::expected<lazy, error>`. A null pointer or an empty
owner throws `std::logic_error`. A `std::string` lvalue goes to the form
with `std::string_view` and is copied. The form with an owner does not take
a temporary `std::string`.

`lazy::from` checks only the limit `input_bytes`. It does not check that
the bytes are one complete item. It accepts `82 01`, an array of two
elements with one element, and a read of the missing element gives
`too_little_data`. It accepts bytes after the item, and no step reads
them.

## 2. Steps and searches in a map or an array

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
// {"cars": [{"hp": 5}, {"hp": 7}], 7: "seven"}
auto const doc = *cbor::lazy::from(bytes);
doc.at(cbor::key{7});                     // the lazy of "seven"
doc.find("cars");                         // the iterator of the pair "cars"
doc.count(std::int64_t{7});               // 1
doc.empty();                              // false
doc.at("cars")->at(5).error();            // index_out_of_bounds
doc.at(0).error();                        // not_indexable, a map has no index
```

A key is a text key or an integer key. A text key does not match a byte
string with the same bytes. A key under tag 28 or tag 29 matches the key
that it marks or names.

A map with a key twice is no error. `at`, `find` and `contains` give the
first pair. `count` counts every pair, as `std::multimap::count` does.

A target that is not in the input gives `too_little_data` at the step,
not at a later read.

## 3. elements and entries

`elements()` gives the elements of an array. `entries()` gives the pairs of
a map, in the order of the bytes. Each is a view: a forward range, a sized
range and a borrowed range. The standard algorithms and views take them.

| Call | Element of the view |
|---|---|
| `elements()` | `std::expected<lazy, error>` |
| `entries()` | `std::expected<std::pair<lazy, lazy>, error>` |

```cpp
// [1, "a", [2]]
auto const l = *cbor::lazy::from(bytes);
for (auto const e : *l.elements())
    e->get<std::int64_t>();               // 1, then incorrect_type, then incorrect_type
l.elements()->size();                     // 3
// {"a": 1, 2: "b"}
auto const m = *cbor::lazy::from(bytes);
std::ranges::distance(*m.entries() | std::views::drop(1));  // 1
```

An algorithm takes the view directly. Each element is a
`std::expected<lazy, error>`, and `->` reaches the lazy:

```cpp
// [{"age": 31}, {"age": 20}, {"age": 40}]
auto const people = *cbor::lazy::from(bytes);
std::ranges::count_if(*people.elements(), [](std::expected<cbor::lazy, cbor::error> const &e) {
    auto const age = e->at("age");
    return age && age->get<std::int64_t>().value_or(0) > 30;
});                                       // 2
```

`elements()` of a map and `entries()` of an array give `not_indexable`.
An array or a map of indefinite length gives `indefinite_length`.

An element that cannot be read gives its error, and the iterator then
stands at the end. A view gives at most `size()` values.

An iterator holds the top-level item. It stays valid after the lazy and the
view that gave it end. An iterator belongs to one thread
([basics.md](basics.md)).

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
// {"b": 2, "d": 4, "f": 6, "h": 8}
auto const m = *cbor::lazy::from(bytes);
auto const e = *m.entries();
auto const h = *m.find("h");
cbor::lazy::find(e.begin(), h, "d");             // the pair "d": 4
cbor::lazy::find(h, std::default_sentinel, "d"); // the end
cbor::lazy::equal_range(e.begin(), h, "f");      // one pair, "f": 6
cbor::lazy::equal_range(e.begin(), h, "e");      // empty, at "f"
```

Two iterators of two different maps throw `std::logic_error`.

## 5. get

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

| bytes | Call | Result |
|---|---|---|
| `1.5` as a binary16, `f9 3e 00` | `get<double>()` | `1.5` |
| `42` | `get<double>()` | `42.0` |
| `null` | `get<std::nullptr_t>()` | `nullptr` |
| `undefined` | `get<cbor::simple_value>()` | `cbor::simple_value::undefined` |
| `2(h'05')` | `get<std::int64_t>()` | `5` |
| `69(h'07000800')` | `get<cbor::typed_array>()` | tag 69 and 4 bytes |

`get` follows a tag 29 to the item that it names, and passes a tag 28.

## 6. decode and lazy_decode

`lazy::decode()` gives `std::expected<std::shared_ptr<cbor::item const>,
error>`. It reads the whole item at the lazy and checks every head. A
`cbor::item` holds the major type, the additional information, the
argument and the content of one item. A string in it is a view into the
bytes.

```cpp
// [1, 2]
auto const i = cbor::lazy::from(bytes)->decode();
(*i)->major_type;   // major_type::array
(*i)->argument;     // 2
```

`lazy_decode(binding, l)` reads the whole item at `l` into the values of
a binding. A binding is a type of your own with a member type `value`.
The library calls the binding for each item: `decode.hpp` lists the calls.
The binding is an lvalue. In the examples, `binding` is an object of such
a type.

```cpp
// {"a": [1, 2]}
lazy_decode(binding, *cbor::lazy::from(bytes));   // the value {"a": [1, 2]} of the binding
// 82 01, an array of two elements with one element
lazy_decode(binding, *cbor::lazy::from(bytes));   // too_little_data
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
  the writer comes back as a `cbor::error` ([basics.md](basics.md)).

```cpp
std::string out;
cbor::encode(binding, out, value);   // value [1, 2]: out holds 82 1b 00 00 00 00 00 00 00 01
                                     //                1b 00 00 00 00 00 00 00 02
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
// 1, "a", [1, 2]
for (auto const e : cbor::sequence{bytes})
    *e;   // the bytes of 1, then of "a", then of [1, 2]
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
of RFC 8949 section 8. A head that is longer than the preferred one
carries its encoding indicator.

| bytes | Result |
|---|---|
| `19 00 01`, the integer 1 in a head of three bytes | `"1_1"` |
| `82 01`, an array of two elements with one element | `too_little_data` |

## 10. item_size

`item_size(encoded)` gives the size in bytes of the first top-level item,
as `std::expected<std::size_t, error>`. It checks that the item is
well-formed and allocates nothing. The nesting depth does not limit it.

| bytes | Result |
|---|---|
| `01 02`, two items | `1` |
| `82 01`, an array of two elements with one element | `too_little_data` |

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
// [28(1), 29(0)]
cbor::lazy::from(bytes)->at(1)->get<std::int64_t>();        // 1
cbor::diagnostic_notation(bytes);                           // "[28(1), 29(0)]"
// [29(0), 28(5)]
lazy_decode(binding, *cbor::lazy::from(bytes));             // sharedref_index_not_marked
```

## 12. Tag 55799

Tag 55799 (RFC 8949 3.4.6) marks bytes as CBOR. `lazy::from`, `decode`,
`lazy_decode` and a path read one leading tag 55799 as its content, in
every width of the argument. A second tag 55799, and a tag 55799 inside an
item, stay tags.

```cpp
// 55799([1, 2])
cbor::lazy::from(bytes)->at(1)->get<std::int64_t>();   // 2
// [55799(1)]
lazy_decode(binding, *cbor::lazy::from(bytes));        // [55799(1)]
```

The encoder writes a tag 55799 that the binding gives as `d9 d9 f7`.
