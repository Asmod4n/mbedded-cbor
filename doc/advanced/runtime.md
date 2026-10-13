# Run-time API in detail

This page adds the details to [../runtime.md](../runtime.md). Every
function is in the namespace `cbor`. A comment shows the CBOR in `bytes`
as text. Where the bytes themselves matter, a comment gives them in hex.

## 1. lazy::copy and lazy::view

A `cbor::lazy` is a position in one top-level item. `lazy::copy` and
`lazy::view` make the lazy of the top-level item.

| Form | The bytes |
|---|---|
| `lazy::copy(std::string&&)` | moved into a new owner, no copy |
| `lazy::copy(std::string_view)` | copied into a new owner |
| `lazy::view(std::shared_ptr<std::string const>)` | the shared pointer is the owner, no copy |

```cpp
auto const a = cbor::lazy::copy(std::move(bytes));         // moved
auto const b = cbor::lazy::copy(std::string_view{bytes});  // copied
auto const held = std::make_shared<std::string const>(bytes);
auto const c = cbor::lazy::view(held);                     // no copy
```

Each form gives `std::expected<lazy, error>`. A null pointer or an empty
owner throws `std::logic_error`. A `std::string` lvalue and a literal go
to the form with `std::string_view` and are copied. `lazy::view` takes
no `std::string` and no `std::string_view`.

`lazy::copy` and `lazy::view` check only the limit `input_bytes`. It does not check that
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
auto const doc = *cbor::lazy::copy(bytes);
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
auto const l = *cbor::lazy::copy(bytes);
for (auto const e : *l.elements())
    e->get<std::int64_t>();               // 1, then incorrect_type, then incorrect_type
l.elements()->size();                     // 3
// {"a": 1, 2: "b"}
auto const m = *cbor::lazy::copy(bytes);
std::ranges::distance(*m.entries() | std::views::drop(1));  // 1
```

An algorithm takes the view directly. Each element is a
`std::expected<lazy, error>`, and `->` reaches the lazy:

```cpp
// [{"age": 31}, {"age": 20}, {"age": 40}]
auto const people = *cbor::lazy::copy(bytes);
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
auto const m = *cbor::lazy::copy(bytes);
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

## 6. decode

`lazy::decode()` gives `std::expected<std::shared_ptr<cbor::item const>,
error>`. It reads the whole item at the lazy, checks every head and builds
one `cbor::item` for each data item. An item is an aggregate of 24 bytes:
`type` (a `cbor::type`), `additional_information` (RFC 8949 3) and a union
`via`. The type says which member of `via` holds the value. A string in it
is a view into the bytes.

| `type` | Member of `via` |
|---|---|
| `unsigned_integer`, `negative_integer` | `u64`, the argument |
| `byte_string`, `text_string` | `bin` (`std::span<std::byte const>`), `str` (`std::string_view`) |
| `array` | `array`, a `std::span<cbor::item const>` of the elements |
| `map` | `map`, a `std::span<cbor::item const>` with each key and its value one after the other |
| `tag`, `unsigned_bignum`, `negative_bignum`, `typed_array` | `tag`: `number` and `content`, the item of the content |
| `half_float`, `single_float`, `double_float` | `f64`, the value as a `double` |
| `false_value`, `true_value`, `null`, `undefined`, `simple_value` | `simple_value` |

```cpp
// [1, {"a": 2}]
auto const i = cbor::lazy::copy(bytes)->decode();
(*i)->type;               // type::array
(*i)->via.array.size();   // 2
// 82 01, an array of two elements with one element
cbor::lazy::copy(bytes)->decode();                       // too_little_data
```

A tag 28 stays a tag item over the value it marks. A tag 29 stays a tag
item over its content. The decode reads no tag 29: it keeps the offset of
each tag 28 in the order of the input, and checks nothing about a tag 29.
`cbor::shared_value_resolve(tree, item)` takes the tree and a tag 29 of it
and gives a `std::expected<cbor::lazy, error>`. The lazy views the input at
the value that the tag 29 names, after any chain of tags 28, 29 and 55799.
Nothing is copied and the tree does not change. An index that names no tag
28 before the tag 29 gives `sharedref_index_not_marked`. A content that is
not an unsigned integer gives `inadmissible_type_for_tag_content`. A chain
with no value gives `sharedref_not_complete`. When the tag 29 is the content
of another tag, the reader of that tag checks the content constraint of that
tag. `tree` is the result of the decode. No read of the library follows a
tag 29 by itself, so a cycle ends. A struct reader, a lazy and `at_path`
resolve a tag 29 when they read it, with the same function. A tag 24 is a
tag item over its byte string, and `cbor::encoded_cbor_data_item(tree,
item)` gives a `std::expected` of the root of the embedded item, decoded as
a top-level item of its own. A tag 29 inside the embedded item names a tag
28 inside it. For a tag 24 over a tag 29 it gives `incorrect_type`. Both
functions throw `std::logic_error` for an item that is not a tag 29 or tag
24 of that tree. A count in a head reserves nothing: an array that claims more
elements than bytes are left gives `too_little_data` before it reads an
element.

`type()` of a lazy and of an item gives `std::expected<cbor::type, error>`:
the major type, or for a tag 2, 3 or a typed array of RFC 8746 the kind of
that tag. A lazy reads through tags 28 and 29; an item keeps them as tags.

A bignum (tag 2 or 3) whose magnitude fits in 64 bits is the same value
as a basic integer (RFC 8949 3.4.3). `decode()` makes it an
`unsigned_integer` or `negative_integer` item, and `type()` of a lazy
gives that type. A larger bignum stays `unsigned_bignum` or
`negative_bignum`. A key 2(h'01') is the same key as 1.

Such a bignum is a number everywhere: `type()`, `get<int>`, `get<double>`,
a path and a key lookup read it as the integer, and no read gives it as a
tag. A bignum larger than 64 bits stays a tag item. Its content is the
magnitude as raw bytes, a span into the input. The library has no type
for big numbers and depends on no library for them.

The decode reads every head of the item, so every limit counts each head.
It reads no byte after the item.

## 7. encode

`cbor::encoder` writes one data item per call, in the order of the bytes:
`unsigned_integer`, `negative_integer`, `byte_string`, `text_string`,
`array`, `map`, `tag`, `shareable`, `sharedref`, `simple_value`,
`floating_point` and `typed_array`.
An integer and a float take the width of their type; a count, a length and
a tag number take the shortest head. The encoder counts nothing: the counts
of `array` and `map` are yours.

Each call gives `std::expected<void, error>`. After an error, and after
`finish()`, every call throws `std::logic_error`. `finish()` gives the size
in bytes, or the pieces for an `iov_writer`.

The target of the encoder is one of:

- a container of bytes, for example `std::string` or
  `std::vector<std::byte>`: the encoder appends to it;
- a fixed span of bytes: the encoder writes into it, allocates nothing, and
  gives `no_buffer_space` when an item does not fit;
- a `cbor::iov_writer` ([13](#13-encode_iov));
- a writer of your own with `allocate`, `append` and `done`. An error of
  the writer comes back as a `cbor::error` ([basics.md](basics.md)).

```cpp
std::string out;
cbor::encoder e(out);
e.array(2);
e.unsigned_integer(std::uint8_t{1});
e.text_string("a");
e.finish();   // 5: out holds 82 18 01 61 61
```

`shareable()` writes tag 28, and the next item is the shared value.
`sharedref(n)` writes tag 29 with the index `n` in the shortest head. The
index counts the tags 28 in the order of the bytes, from 0
([value-sharing](http://cbor.schmorp.de/value-sharing)).

Tags 28 and 29 exist only to write cycles. An application writes tag 28
only on a value that a cycle reaches again, and tag 29 only for the
reference that closes the cycle. A value that two places hold without a
cycle is written in full at each place.

```cpp
std::string out;
cbor::encoder e(out);
e.shareable();
e.array(1);
e.sharedref(0);
e.finish();   // 6: out holds d8 1c 81 d8 1d 00
```

A cycle through a tag that is not an array or a map is allowed. For
example, `28(6(29(0)))` decodes into a tree where the content of tag 6 is
a reference to that tag 6 item.

`cbor::encode(item, target)` writes an item with the additional
information that each item keeps. Each integer, length and tag number
keeps the width of its head on the wire, tags 28 and 29 included. A float
keeps the width of its type, and a NaN keeps its bits. A bignum that fits
in 64 bits is decoded as a basic integer, and is written in its shortest
head. It gives the size in bytes.

## 8. cbor::sequence

`cbor::sequence` reads a CBOR Sequence (RFC 8742): top-level items one
after the other, with nothing between them. Its iterator gives
`std::expected<std::string_view, error>`, the bytes of one item.

```cpp
// 1, "a", [1, 2]
for (auto const e : cbor::sequence{owner})   // owner: std::shared_ptr<std::string const>
    *e;   // the bytes of 1, then of "a", then of [1, 2]
```

- A last item that is not complete gives `too_little_data`. The iterator
  keeps the bytes that are left in its member `encoded`, so a reader of a
  stream can read them again with more bytes.
- An item that is not well-formed gives its error, and the sequence ends
  there.
- The sequence takes only a `std::shared_ptr<std::string const>`, and
  keeps it alive.

Each element is a top-level item of its own. A tag 29 in one element does
not reach a tag 28 in another. `lazy::copy(*e)` reads an element; it
copies the element, because `lazy::view` takes a whole `std::string`.

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
- A tag 29 inside the tag 28 that it names is a cycle. A step and
  `decode` read it; the tree holds the cycle as a pointer. A tag 29 that is
  itself the content of the tag 28 that it names names no value, and a step
  gives `sharedref_not_complete`.
- `diagnostic_notation` writes the tags as they are.

```cpp
// [28(1), 29(0)]
cbor::lazy::copy(bytes)->at(1)->get<std::int64_t>();        // 1
cbor::diagnostic_notation(bytes);                           // "[28(1), 29(0)]"
// [29(0), 28(5)]
cbor::lazy::copy(bytes)->decode();                          // sharedref_index_not_marked
```

## 12. Tag 55799

Tag 55799 (RFC 8949 3.4.6) marks bytes as CBOR. `lazy::copy`, `lazy::view`, `decode`
and a path read one leading tag 55799 as its content, in
every width of the argument. A second tag 55799, and a tag 55799 inside an
item, stay tags.

```cpp
// 55799([1, 2])
cbor::lazy::copy(bytes)->at(1)->get<std::int64_t>();   // 2
// [55799(1)]
cbor::lazy::copy(bytes)->decode();                     // an array that holds the tag item 55799(1)
```

`e.tag(55799)` writes `d9 d9 f7`.

## 13. encode_iov

`cbor::encode_iov(item)` writes a decoded item into a list of pieces, not
into one buffer. It gives
`std::expected<cbor::owning_ref<std::span<std::string_view const>>, error>`:
the pieces in order, and their storage, which lives as long as the
result. Use the pieces with `writev` or a socket, or join them.

```cpp
auto const pieces = cbor::encode_iov(**cbor::lazy::copy(bytes)->decode());
for (std::string_view const piece : **pieces)
    send(piece);
```

The encoder writes into blocks of 16 KiB and copies each byte once; a
`std::string` that grows would copy its bytes at each doubling.

`cbor::encoder` over a `cbor::iov_writer` writes item by item. A text or a
byte string of 32 bytes or more that you hand over with its owner is not
copied: `std::string&&` and `std::vector<std::byte>&&` are moved into the
result, a `std::shared_ptr` or a `cbor::owning_ref` is kept. `finish()`
gives the pieces.

```cpp
cbor::iov_writer w;
cbor::encoder e(w);
e.text_string(std::move(long_text));   // moved, not copied
auto const pieces = e.finish();
```
