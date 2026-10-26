# Run-time API in detail

This page adds the details to [../runtime.md](../runtime.md). Every
function is in the namespace `cbor`. A comment shows the CBOR in `bytes`
as text. Where the bytes themselves matter, a comment gives them in hex.

## 1. lazy::take_ownership and lazy::view

A `cbor::lazy` is a position in one top-level item.
`lazy::take_ownership` and `lazy::view` make the lazy of the top-level
item. No form copies the bytes.

| Form | The bytes |
|---|---|
| `lazy::take_ownership(std::string&&)` | moved into a new owner |
| `lazy::view(std::shared_ptr<std::string const>)` | the shared pointer is the owner |
| `lazy::view(std::shared_ptr<void const>, view)` | read in place; the first argument keeps them alive |

```cpp
auto const a = cbor::lazy::take_ownership(std::move(bytes));   // moved
auto const held = std::make_shared<std::string const>(message());
auto const b = cbor::lazy::view(held);                          // read in place
```

Each form gives `std::expected<lazy, error>`. A null pointer or an empty
owner throws `std::logic_error`. A `std::string` lvalue, a
`std::string_view` alone and a literal do not compile.

`lazy::take_ownership` and `lazy::view` check no limit. They do not check that
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
| `l[key]`, `l[cbor::key{n}]`, `l[index]` | the same as `at` |
| `size()` | `std::expected<std::uint64_t, error>`, the elements of an array or the pairs of a map |
| `empty()` | `std::expected<bool, error>` |

`operator[]` gives the same `std::expected` as `at` and does not throw.
`l["a"]["b"]` does not compile, because the result is a `std::expected`.
A path reads a chain: `l.at_path<"$.a.b">()` ([path.md](path.md)).

```cpp
// {"cars": [{"hp": 5}, {"hp": 7}], 7: "seven"}
auto const doc = *cbor::lazy::take_ownership(std::move(bytes));
doc.at(cbor::key{7});                     // the lazy of "seven"
doc["cars"];                              // the lazy of [{"hp": 5}, {"hp": 7}]
doc.at("bus").error();                    // key_not_found
doc.empty();                              // false
doc.at("cars")->at(5).error();            // index_out_of_bounds
doc.at(0).error();                        // not_indexable, a map has no index
```

A key is a text key or an integer key. A text key does not match a byte
string with the same bytes. A key under tag 28 or tag 29 is a tag, and
matches no text key and no integer key.

A map with a key twice is no error. `at`, `operator[]` and `at_path` give
the first pair.

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
// owner: a std::shared_ptr<std::string const> with the bytes in the comment above each read
// [1, "a", [2]]
auto const l = *cbor::lazy::view(owner);
for (auto const e : *l.elements())
    e->get<std::int64_t>();               // 1, then incorrect_type, then incorrect_type
l.elements()->size();                     // 3
// {"a": 1, 2: "b"}
auto const m = *cbor::lazy::view(owner);
std::ranges::distance(*m.entries() | std::views::drop(1));  // 1
```

An algorithm takes the view directly. Each element is a
`std::expected<lazy, error>`, and `->` reaches the lazy:

```cpp
// [{"age": 31}, {"age": 20}, {"age": 40}]
auto const people = *cbor::lazy::take_ownership(std::move(bytes));
std::ranges::count_if(*people.elements(), [](std::expected<cbor::lazy, cbor::error> const &e) {
    auto const age = e->at("age");
    return age && age->get<std::int64_t>().value_or(0) > 30;
});                                       // 2
```

`elements()` of a map and `entries()` of an array give `not_indexable`.
An array or a map of indefinite length gives `indefinite_length`.

An element that cannot be skipped gives its error at the next
position, and the iterator gives the same error at each later position.
A view gives exactly `size()` values.

```cpp
// 83 1c 00 00: an array of three elements; 1c is no valid head
auto const bad = *cbor::lazy::view(owner);
for (auto const e : *bad.elements())
    e.has_value();                        // true, then false, then false
```

The first element is a lazy at `1c`, and a read of it gives its error.

An iterator holds the top-level item. It stays valid after the lazy and the
view that gave it end. An iterator belongs to one thread
([basics.md](basics.md)).

## 4. get

| T | Item | Result |
|---|---|---|
| an integer type, not `bool` | an integer, or a bignum (tag 2, 3) that fits T | T |
| `double` | a float of 16, 32 or 64 bits, or an integer from -2^53 to 2^53 | `double` |
| `bool` | `false`, `true` | `bool` |
| `std::nullptr_t` | `null` | `nullptr` |
| `cbor::simple_value` | a simple value | the value |
| `std::string_view` | a text string | `ref<std::string_view>` |
| `std::span<std::byte const>` | a byte string | `ref<std::span<std::byte const>>` |
| `cbor::typed_array` | a typed array (RFC 8746) | `ref<cbor::typed_array>` |

| bytes | Call | Result |
|---|---|---|
| `1.5` as a binary16, `f9 3e 00` | `get<double>()` | `1.5` |
| `42` | `get<double>()` | `42.0` |
| `null` | `get<std::nullptr_t>()` | `nullptr` |
| `undefined` | `get<cbor::simple_value>()` | `cbor::simple_value::undefined` |
| `2(h'05')` | `get<std::int64_t>()` | `5` |
| `69(h'07000800')` | `get<cbor::typed_array>()` | tag 69 and 4 bytes |

`get` passes a tag 28 and a tag 29 to their content, as it passes every tag that it does not interpret.

## 5. clone and decode

`lazy::clone()` gives `std::expected<cbor::owning<cbor::item>, error>`.
It reads the whole item at the lazy, checks every head and builds one
`cbor::item` for each data item. `cbor::decode(encoded)` does the same
for the one top-level item in a `std::string_view`. The tree copies each
text and byte string into its own memory zone. It does not view the
input, and it stays valid after the input ends.

An item has two data members: `type` (a `cbor::type`) and
`additional_information` (RFC 8949 3). `get<K>()` gives the value as
`std::expected<cbor::item_alternative_t<K>, error>`, where `K` is a
`cbor::type`. A `K` that is not the `type` of the item gives
`incorrect_type`.

| `type` | `get<type>()` gives |
|---|---|
| `unsigned_integer`, `negative_integer` | `std::uint64_t`, the argument |
| `byte_string`, `text_string` | `std::span<std::byte const>`, `std::string_view` |
| `array` | `std::span<cbor::item const>`, the elements |
| `map` | `std::span<cbor::item const>`, each key and its value one after the other |
| `tag`, `unsigned_bignum`, `negative_bignum`, `typed_array` | `std::pair<std::uint64_t, cbor::item const *>`, the tag number and the item of the content |
| `half_float`, `single_float`, `double_float` | `double` |
| `false_value`, `true_value`, `null`, `undefined`, `simple_value` | `cbor::simple_value` |

```cpp
// owner: a std::shared_ptr<std::string const> with the bytes in the comment above each read
// [1, {"a": 2}]
auto const tree = cbor::lazy::view(owner)->clone();
(*tree)->type;                                   // cbor::type::array
(*tree)->get<cbor::type::array>()->size();       // 2
// 82 01, an array of two elements with one element
cbor::lazy::view(owner)->clone().error();        // too_little_data
```

A tag 28 stays a tag item over the value it marks. A tag 29 stays a tag
item over its content. The decode resolves neither tag and checks no index.
A tag 24 is a tag item over its byte string, and
`cbor::encoded_cbor_data_item(tree, item)` gives a `std::expected` of the
root of the embedded item, decoded as a top-level item of its own. It throws
`std::logic_error` for an item that is not a tag 24 of that tree. For a tag
24 whose content is no byte string it gives `incorrect_type`. A count in a head reserves nothing: an array that claims more
elements than bytes are left gives `too_little_data` before it reads an
element.

`type()` of a lazy gives `std::expected<cbor::type, error>`: the major
type, or for a tag 2, 3 or a typed array of RFC 8746 the kind of that
tag. The data member `type` of an item holds the same value. A lazy and
an item keep tags 28 and 29 as tags.

A bignum (tag 2 or 3) whose magnitude fits in 64 bits is the same value
as a basic integer (RFC 8949 3.4.3). `clone` and `decode` make it an
`unsigned_integer` or `negative_integer` item, and `type()` of a lazy
gives that type. A larger bignum stays `unsigned_bignum` or
`negative_bignum`. A key 2(h'01') is the same key as 1.

Such a bignum is a number everywhere: `type()`, `get<int>`, `get<double>`,
a path and a key lookup read it as the integer, and no read gives it as a
tag. A bignum larger than 64 bits stays a tag item. Its content is a
byte string item that holds a copy of the magnitude. The library has no
type for big numbers and depends on no library for them.

The decode reads every head of the item, so every limit counts each head.
`lazy::clone` and `cbor::decode` read no byte after the item, so a
CBOR sequence (RFC 8742) is read item by item.

## 6. encode

`cbor::encoder` writes one data item per call, in the order of the bytes:
`unsigned_integer`, `negative_integer`, `byte_string`, `text_string`,
`array`, `map`, `tag`, `simple_value`, `floating_point` and
`typed_array`.
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

Tags 28 and 29 mark shared values. `shareable()` and `sharedref()` of
the encoder are private, and no function of the library calls them.
`tag(28)` and `tag(29)` write them as any other tag. `cbor::structs`
encode writes no tag 28 and no tag 29.

`cbor::encode(item, target)` writes an item with the additional
information that each item keeps. Each integer, length and tag number
keeps the width of its head on the wire, tags 28 and 29 included. A float
keeps the width of its type, and a NaN keeps its bits. A bignum that fits
in 64 bits is decoded as a basic integer, and is written in its shortest
head. It gives the size in bytes.

## 7. cbor::sequence

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
- The sequence takes a `std::shared_ptr<std::string const>`, or a
  `std::shared_ptr<void const>` and a `std::string_view` of the bytes
  that it keeps alive. It keeps the owner alive.

Each element is a top-level item of its own. A tag 29 in one element does
not reach a tag 28 in another. `lazy::view(owner, *e)` reads an element
in place; `owner` is the `std::shared_ptr` of the sequence.

## 8. diagnostic_notation

`diagnostic_notation(encoded)` gives the item in the diagnostic notation
of RFC 8949 section 8. A head that is longer than the preferred one
carries its encoding indicator.

| bytes | Result |
|---|---|
| `19 00 01`, the integer 1 in a head of three bytes | `"1_1"` |
| `82 01`, an array of two elements with one element | `too_little_data` |

## 9. item_size

`item_size(encoded)` gives the size in bytes of the first top-level item,
as `std::expected<std::size_t, error>`. It checks that the item is
well-formed and allocates nothing. The nesting depth does not limit it.

| bytes | Result |
|---|---|
| `01 02`, two items | `1` |
| `82 01`, an array of two elements with one element | `too_little_data` |

## 10. Tags 28 and 29

Tag 28 marks a value as shared. Tag 29 with the index n stands for the
value of the n-th tag 28 ([value-sharing](http://cbor.schmorp.de/value-sharing)).

- A lazy, `clone`, `decode`, `at_path`, `query` and `diagnostic_notation` read tag 28
  as a tag over its content and tag 29 as a tag over its content. They
  resolve nothing, check no index and give no error of their own. A step
  and `get` pass both tags, as they pass every tag that they do not
  interpret.
- `cbor::structs` decode resolves them, with a table that exists for one
  call. See doc/advanced/structs.md.
- A lazy holds only its owner, its bytes and its offset. Several threads
  read one lazy at the same time.

```cpp
// owner: a std::shared_ptr<std::string const> with the bytes in the comment above each read
// [28(1), 29(0)]
cbor::lazy::view(owner)->at(1)->type();                     // cbor::type::tag
cbor::lazy::view(owner)->at(1)->get<std::int64_t>();        // 0
cbor::diagnostic_notation(*owner);                           // "[28(1), 29(0)]"
```

## 11. Tag 55799

Tag 55799 (RFC 8949 3.4.6) marks bytes as CBOR. `lazy::take_ownership`
and `lazy::view` read one leading tag 55799 as its content, in every
width of the argument. So `clone` of that lazy gives the content.
`cbor::decode` keeps a leading tag 55799 as a tag item. A second tag
55799, and a tag 55799 inside an item, stay tags.

```cpp
// owner: a std::shared_ptr<std::string const> with the bytes in the comment above each read
// 55799([1, 2])
cbor::lazy::view(owner)->at(1)->get<std::int64_t>();   // 2
cbor::lazy::view(owner)->clone();                      // the array item [1, 2]
cbor::decode(*owner);                                  // the tag item 55799([1, 2])
// [55799(1)]
cbor::lazy::view(owner)->clone();                      // an array that holds the tag item 55799(1)
```

`e.tag(55799)` writes `d9 d9 f7`.

## 12. encode_iov

`cbor::encode_iov(item)` writes a decoded item into a list of pieces, not
into one buffer. It gives
`std::expected<cbor::ref<std::span<std::string_view const>>, error>`:
the pieces in order, and their storage, which lives as long as the
result. Use the pieces with `writev` or a socket, or join them.

```cpp
auto const tree = cbor::decode(bytes);
auto const pieces = cbor::encode_iov(**tree);
for (std::string_view const piece : **pieces)
    send(piece);
```

The encoder writes into blocks of 16 KiB and copies each byte once; a
`std::string` that grows would copy its bytes at each doubling.

`cbor::encoder` over a `cbor::iov_writer` writes item by item. A text or a
byte string of 32 bytes or more that you hand over with its owner is not
copied: `std::string&&` and `std::vector<std::byte>&&` are moved into the
result, a `std::shared_ptr` or a `cbor::ref` is kept. `finish()`
gives the pieces.

```cpp
cbor::iov_writer w;
cbor::encoder e(w);
e.text_string(std::move(long_text));   // moved, not copied
auto const pieces = e.finish();
```
