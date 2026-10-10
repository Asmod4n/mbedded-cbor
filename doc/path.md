# Paths

A path selects a part of a CBOR data item and reads it without a decode of
the whole item. The syntax is JSONPath, RFC 9535. Inside brackets, a
compile-time path also takes a literal of CBOR diagnostic notation
(draft-ietf-cbor-edn-literals-28), so a key can be any CBOR item.

Every example in this document is a test in `test/doc_path.cpp`. The test
names start with `doc path:`.

## Contents

1. The forms
2. Which types a typed read gives
3. Path syntax
4. Functions
5. CBOR beyond JSON: keys, tags, typed arrays
6. Singular queries
7. Compile time and run time
8. Errors
9. Limits
10. Cost
11. Threads
12. What RFC 9535 has and this library does not have

## 1. The forms

| Form | Path | Result |
|---|---|---|
| `at_path<Path, T>(encoded)` | compile time, singular | `std::expected<T, error>`, T a scalar |
| `at_path<Path, T>(owner, encoded)` | compile time, singular | `std::expected<owning_ref<T>, error>`, T a view |
| `at_path<Path, T>(lazy)` | compile time, singular | T a scalar: `T`. T a view: `owning_ref<T>` |
| `at_path<Path>(binding, lazy)` | compile time, singular | `Binding::value` |
| `at_path(binding, path, lazy)` | run time, singular | `Binding::value` |
| `query<Path>(binding, lazy)` | compile time, any | an array of `Binding::value` |
| `query(binding, path, lazy)` | run time, any | an array of `Binding::value` |
| `schema<T>::at_path<Path>(encoded, index...)` | compile time, over the members of T | the type of the member |

Every function is in the namespace `cbor`. Every function returns a
`std::expected` and does not throw for bad input. A path is a string
literal for the compile-time forms and a `std::string_view` for the
run-time forms.

### A scalar from bytes

```cpp
std::string const bytes = /* {"user": {"id": 42, "score": 2.5, "admin": true}} */;
cbor::at_path<"$.user.id", std::int64_t>(bytes);   // 42
cbor::at_path<"$.user.score", double>(bytes);      // 2.5
cbor::at_path<"$.user.admin", bool>(bytes);        // true
```

The function reads the bytes in place. It does not copy them and does not
keep them. The result is a value, so it does not point into the bytes.

### A string, a byte string or a typed array

A view points into the bytes. So this form takes an owner of the bytes,
and the result holds the owner. The view stays valid while the result
lives.

```cpp
auto const owner = std::make_shared<std::string const>(bytes);
auto const name = cbor::at_path<"$.name", std::string_view>(owner, *owner);
if (name)
    use(**name);   // owning_ref<std::string_view>: * gives the view
```

`T` is one of `std::string_view`, `std::span<std::byte const>` and
`cbor::typed_array`. The owner is a `std::shared_ptr<void const>`. A view
type without an owner does not compile. A temporary `std::string` or a
moved `std::string` beside the owner does not compile. An empty owner
throws `std::logic_error`.

The function trusts that the owner holds the bytes. It does not check this
(doc/known-limits.md).

### A path from a lazy

A `cbor::lazy` is a position in an item. A path from a lazy starts at that
position, so `$` is the node of the lazy and not the top-level item.

```cpp
auto const cars = cbor::lazy::from(bytes)->at("cars");
cbor::at_path<"$[1].hp", std::int64_t>(*cars);             // 7
cbor::at_path<"$[1].make", std::string_view>(*cars);       // owning_ref, "kia"
```

The view form needs no separate owner here: the lazy holds the bytes, and
the result holds the lazy's top-level item.

### Any value into your own type

A binding is your type that builds values (see `binding.hpp` and
`decode.hpp`). With a binding, the result can be any CBOR item: a map, an
array, a tag.

```cpp
auto const l = *cbor::lazy::from(bytes);   // {"a": [1, 2, 3]}
cbor::at_path<"$.a">(binding, l);          // [1, 2, 3]
cbor::at_path(binding, "$.a[2]", l);       // 3, path at run time
cbor::query<"$.a[*]">(binding, l);         // [1, 2, 3]
cbor::query(binding, "$.a[?@ > 1]", l);    // [2, 3]
cbor::query(binding, "$.b", l);            // [], no error
```

`at_path` gives the value of one node. `query` gives the nodelist as an
array of the binding, also for a singular path and for an empty nodelist.

### A path over a schema message

`cbor::schema<T>` writes a struct in a form with a fixed offset for each
member. `schema<T>::at_path` reads one member and goes to its offset. It
needs reflection, so it compiles only with g++ (doc/known-limits.md).

```cpp
struct [[=cbor::tag(1700)]] engine { std::uint16_t hp; std::uint32_t cc; };
struct [[=cbor::tag(1701)]] car { std::string make; engine motor; std::vector<engine> spares; };

auto const bytes = *cbor::schema<car>::encode(car{"kia", {120, 1600}, {{90, 1200}, {75, 1000}}});
cbor::schema<car>::at_path<"$.motor.cc">(bytes);        // 1600
cbor::schema<car>::at_path<"$.spares[1].hp">(bytes);    // 75
cbor::schema<car>::at_path<"$.spares[].hp">(bytes, 0);  // 90, index at run time
```

The path names members of `T`. An index is a number or `[]`. Each `[]` takes
one run-time index argument, in order. A path to a member that `T` does not
have does not compile. The static form does not give a text. For a text, use
the accessor: `schema<car>::path(owner, bytes)->at_path<"$.make">()` gives a
`std::string_view`. This path is not JSONPath: it has no wildcard, no
filter and no descendant segment.

## 2. Which types a typed read gives

| T | CBOR item | Other items |
|---|---|---|
| a signed or unsigned integer type, also `char` | major type 0 or 1, tag 2 or 3 with a magnitude up to 64 bits | `incorrect_type`; out of range: `number_out_of_range` |
| `double` | a half, single or double float | `incorrect_type` |
| `bool` | `true`, `false` | `incorrect_type` |
| `std::nullptr_t` | `null` | `incorrect_type` |
| `cbor::simple_value` | a simple value | `incorrect_type` |
| `std::string_view` | a text string | `incorrect_type` |
| `std::span<std::byte const>` | a byte string | `incorrect_type` |
| `cbor::typed_array` | a tag of RFC 8746 over a byte string | `incorrect_type` |

A typed read does not convert. An integer is not a `double`, and a float is
not an integer:

```cpp
cbor::at_path<"$", std::uint8_t>(/* 256 */);   // number_out_of_range
cbor::at_path<"$", unsigned>(/* -1 */);        // number_out_of_range
cbor::at_path<"$", double>(/* 1 */);           // incorrect_type
cbor::at_path<"$", std::int64_t>(/* 1.0 */);   // incorrect_type
```

Any other T does not compile, for example `float`, `std::string` or a view
without an owner. For any other result, use a binding.

## 3. Path syntax

The examples use this item:

```
{"store": {"book": [{"title": "A", "price": 8},
                    {"title": "B", "price": 12},
                    {"title": "C", "price": 9, "isbn": "x"}],
           "bicycle": {"price": 20}}}
```

| Path | Nodelist | RFC 9535 |
|---|---|---|
| `$` | the item | 2.2 |
| `$.store.bicycle.price` | 20 | 2.3.1, name |
| `$['store']['bicycle']['price']` | 20 | 2.3.1, name in brackets |
| `$.store.book[0].title` | "A" | 2.3.3, index |
| `$.store.book[-1].title` | "C" | 2.3.3, index from the end |
| `$.store.book[*].price` | 8, 12, 9 | 2.3.2, wildcard |
| `$.store.book[0, 2].title` | "A", "C" | 2.5.1, several selectors |
| `$.store.book[0:2].title` | "A", "B" | 2.3.4, slice |
| `$.store.book[::-1].title` | "C", "B", "A" | 2.3.4, slice with step |
| `$..price` | 8, 12, 9, 20 | 2.5.2, descendant |
| `$.store.book[?@.price < 10].title` | "A", "C" | 2.3.5, filter |
| `$.store.book[?@.isbn].title` | "C" | 2.3.5, existence test |
| `$.store.book[?!@.isbn && @.price > 10].title` | "B" | 2.3.5, `!` and `&&` |
| `$.store.book[?@.price == 8 \|\| @.title == 'B'].title` | "A", "B" | 2.3.5, `\|\|` |
| `$.store.book[?@.price > $.store.bicycle.price].title` | empty | 2.3.5, absolute query |

The parts in detail:

- A name in dot form starts with a letter, `_` or a byte of 0x80 or more,
  and goes on with these or digits. A name in brackets is in single or
  double quotes, with the escapes of RFC 9535 2.3.1.
- An index is an integer without leading zeros, from -(2^53 - 1) to
  2^53 - 1. `-0` is not valid.
- A slice is `start:end:step`. Each part can be absent. Step 0 selects
  nothing. A slice of a node that is not an array selects nothing.
- A comparison is `==`, `!=`, `<`, `<=`, `>` or `>=`. A number compares by
  value, so 1 equals 1.0. `<` and its kind compare numbers with numbers and
  strings with strings. A side that selects nothing equals only another
  side that selects nothing.
- `&&` binds more tightly than `||`. `!` negates an existence test or a
  group in parentheses.
- A literal in a run-time filter is a JSON literal: a number, a string,
  `true`, `false` or `null`.
- Blanks are allowed before a segment and inside brackets. A blank before
  `$` or after the last segment is `invalid_path`.

A wildcard on a map selects the values. A name selects only from a map. An
index selects from an array, and from a map with an integer key (see
section 5).

## 4. Functions

| Function | Gives | RFC 9535 |
|---|---|---|
| `length(x)` | the characters of a text, the elements of an array, the pairs of a map; Nothing for other values | 2.4.4 |
| `count(q)` | the number of nodes in the nodelist of q | 2.4.5 |
| `value(q)` | the value of the node when q has one node; Nothing otherwise | 2.4.8 |
| `match(x, re)` | not supported: `invalid_path` | 2.4.6 |
| `search(x, re)` | not supported: `invalid_path` | 2.4.7 |

```cpp
// [{"t": "abc"}, {"t": [1, 2]}, {"t": 5}]
cbor::query(binding, "$[?length(@.t) == 3].t", l);    // ["abc"]
cbor::query(binding, "$[?count(@.t[*]) == 2].t", l);  // [[1, 2]]
cbor::query(binding, "$[?value(@..t) == 5].t", l);    // [5]
cbor::query(binding, "$[?match(@.t, 'a.c')]", l);     // invalid_path
```

The library has no regular expressions, by decision of the owner. A
compile-time path with `match` or `search` does not compile:
`is_valid_path_v<"$[?match(@.t, 'a.c')]">` is false. A function that is not
well-typed (RFC 9535 2.4.3) is `invalid_path`.

## 5. CBOR beyond JSON

### Keys that are not text

A map key in CBOR can be any item. Two ways reach such a key:

- An index selector on a map selects the value under the equal integer key.
  `$[1]` finds the key 1, and `$[-2]` finds the key -2. This is not an index
  from the end: a map has no order in a path.
- A compile-time path takes any EDN literal in brackets: `$[h'01']` for a
  byte string, `$[[1, 2]]` for an array, `$[1.5]`, `$[true]`, `$[1000("x")]`
  for a tag, `$[{"a": 1}]` for a map.

```cpp
// {1: "x", -2: "y", h'01': "z", [1, 2]: "w"}
cbor::at_path<"$[1]">(binding, l);         // "x"
cbor::at_path<"$[-2]">(binding, l);        // "y"
cbor::at_path<"$[h'01']">(binding, l);     // "z"
cbor::at_path<"$[[1, 2]]">(binding, l);    // "w"
cbor::at_path(binding, "$[h'01']", l);     // invalid_path at run time
```

Keys compare by value (RFC 8949 5.6.1). So `h'01'` and `b64'AQ'` name the
same key. A head that is not in preferred form is the same key. An integer
and a float are different keys: `$[1.0]` does not find the key 1.

An EDN literal is a compile-time feature. The run-time path reads RFC 9535
and nothing more. The typed forms `at_path<Path, T>` take only names and
integer indexes; a path with an EDN key needs a binding.

A filter in a compile-time path compares with any EDN literal:
`query<"$[?@ == h'01']">(binding, l)`. A run-time filter with `h'01'` is
`invalid_path`.

When a map has the same key twice, every singular form takes the first
pair. A wildcard, a descendant segment and a filter take all pairs.

### Tags on the way

| Tag | What a path does |
|---|---|
| 24, encoded CBOR data item | goes into the byte string and reads the embedded item |
| 28, shareable | passes it; the value is the content |
| 29, shared reference | reads the value that the reference names |
| 55799, self-described CBOR | passes it, at the top and inside the item |
| 2 and 3, bignum | a typed read into an integer type reads the magnitude |
| an RFC 8746 typed array tag | a typed read into `cbor::typed_array` reads it |
| any other tag | a path does not enter it |

```cpp
cbor::at_path<"$.a[1]", int>(/* {"a": 24(<<[1, 2]>>)} */);          // 2
cbor::at_path<"$[1].k", int>(/* [28({"k": 7}), 29(0)] */);          // 7
cbor::at_path<"$.a", int>(/* 55799({"a": 5}) */);                   // 5
cbor::at_path<"$.v[1]", int>(/* {"v": 1000([1, 2])} */);            // not_indexable
cbor::query(binding, "$.v[*]", /* same */);                         // []
cbor::at_path(binding, "$.v", /* same */);                          // 1000([1, 2])
```

A tag with another number stands for a value with a meaning that the path
does not know. So a singular path through it is `not_indexable`, and a
wildcard selects nothing from it. A path that ends at the tag gives the
whole tagged item to a binding.

Tag 29 can point back to a tag 28 anywhere before it. When a typed read
meets tag 29, it reads the item again through a lazy to find the marks.
This costs an allocation. A tag 29 that points forward is
`sharedref_index_not_marked`.

### Typed arrays

A typed array (RFC 8746) is one value: a tag over a byte string. A path
ends at it and does not enter it.

```cpp
// {"v": 69(h'07000800')}, uint16 little endian [7, 8]
cbor::at_path<"$.v[1]", int>(bytes);                       // not_indexable
cbor::at_path<"$.v", cbor::typed_array>(lazy);             // tag 69, 4 bytes
```

Read the elements from `typed_array::bytes`, or with a schema message and
`schema<T>::at_path`, which reads one element.

## 6. Singular queries

A singular query (RFC 9535 2.3.5.1) has only names and indexes, and no
descendant segment. It selects one node at most. `at_path` reads only a
singular query. `query` reads every query.

```cpp
static_assert(cbor::is_singular_query_v<"$.a[0]['b']">);
static_assert(!cbor::is_singular_query_v<"$.a[*]">);
static_assert(!cbor::is_singular_query_v<"$..a">);
static_assert(!cbor::is_singular_query_v<"$[h'01']">);
static_assert(cbor::is_valid_path_v<"$..a">);
static_assert(!cbor::is_valid_path_v<"$.a[">);
```

- `is_valid_path_v<Path>` is true when the compile-time grammar accepts the
  path, with EDN literals.
- `is_singular_query_v<Path>` is true when the path is valid, singular, and
  each key is a text name. A path with an EDN key is singular for RFC 9535
  but false here, because the typed forms do not take it.

`at_path<Path, T>` with a path that is not singular does not compile.
`at_path<Path>(binding, lazy)` with such a path stops at a `static_assert`
that names `cbor::query`. `at_path(binding, path, lazy)` with such a path
gives `invalid_path`.

## 7. Compile time and run time

| Check | Compile-time path | Run-time path |
|---|---|---|
| Grammar of RFC 9535 | at compile time | at each call: `invalid_path` |
| EDN literal as key or in a filter | accepted | `invalid_path` |
| Singular path for `at_path` | at compile time | at each call: `invalid_path` |
| Segments against the nesting depth | default depth at compile time; depth in force at each call | depth in force at each call |
| Type T of a typed read | at compile time | no typed run-time form |
| Everything about the data | at each call | at each call |

A compile-time path is parsed once, by the compiler. At run time it is a set
of constant arrays. A run-time path is parsed at each call; the parse
allocates. To read the same run-time path many times, keep it short or use
a compile-time path.

## 8. Errors

| Error | When |
|---|---|
| `invalid_path` | The path text is not valid. A run-time `at_path` with a path that is not singular. `match` or `search`. An EDN literal in a run-time path. An index outside ±(2^53 - 1). |
| `key_not_found` | A name or an integer key is not in the map. |
| `index_out_of_bounds` | The index is outside the array. |
| `not_indexable` | A name or an index meets a value that is not a map or an array, for example a text, a number, a typed array or a tag that the path does not pass. A name meets an array. |
| `incorrect_type` | A typed read finds another kind of item than T takes. |
| `number_out_of_range` | The integer does not fit T. |
| `nesting_depth_exceeded` | The path has more segments than the nesting depth in force, or a binding decodes a result that is deeper than it. |
| `string_length_exceeded`, `container_elements_exceeded`, `input_bytes_exceeded` | A limit is in force and the input goes past it (section 9). |
| `too_little_data` | The bytes end before the item ends. |
| `syntax_error`, `indefinite_length` | The bytes are not well-formed, or use indefinite length. |
| `sharedref_index_not_marked`, `sharedref_not_complete` | A tag 29 names no tag 28 before it, or names a tag 28 that is not complete. |
| `inadmissible_type_for_tag_content` | A tag on the way has content that its definition does not allow, for example tag 24 over a number. |
| `nodelist_too_long` | `query` selects more nodes than the item has bytes. This is possible with tag 29. |

```cpp
// {"a": [1, 2], "s": "x"}
cbor::at_path(binding, "$.a[", l);              // invalid_path
cbor::at_path<"$.b", int>(bytes);               // key_not_found
cbor::at_path<"$.a[2]", int>(bytes);            // index_out_of_bounds
cbor::at_path<"$.s[0]", int>(bytes);            // not_indexable
cbor::at_path<"$.s", int>(bytes);               // incorrect_type
cbor::at_path<"$.a[1]", int>(bytes.substr(0, 5)); // too_little_data
```

`query` gives no error when a selector finds nothing: the nodelist is
empty. The errors above are for a singular path, and for bytes that are not
valid in any form.

A wrong use that the compiler cannot see throws `std::logic_error`: an
empty owner, or a `cbor::lazy{}` that holds no item in `at_path` with a
binding and in `query`.

## 9. Limits

The limits are in `cbor::limits`. A path reads them once at each call.

| Limit | What a path does |
|---|---|
| `nesting_depth` (default 128, bound 512) | A path with more segments than the depth is `nesting_depth_exceeded`. A descendant segment that goes deeper than the depth is `nesting_depth_exceeded`. A binding counts the depth of the result from the selected node. An item that a typed read only skips can be deeper: the skip keeps a count and no stack. |
| `string_length` | Every string head that the walk reads is checked, also in a pair it only skips. |
| `container_elements` | Every array and map head that the walk reads is checked, also on the way. |
| `input_bytes` | The typed forms over bytes and `lazy::from` check the size of the bytes. |

```cpp
cbor::limits = {.string_length = 2};
cbor::at_path<"$.n", int>(/* {"long": "xyz", "n": 1} */);   // string_length_exceeded
```

So a limit protects a path also from a large item next to the target. A
compile-time path has at most the default nesting depth of segments, or it
does not compile.

## 10. Cost

A path reads the heads from the start of the node to the target. It does
not decode what it passes:

- In a map, it reads each key until the key matches, and skips each value
  before. A skip reads heads only and jumps over the content of strings.
- In an array, `[i]` skips the i elements before the target. So an index is
  linear in the bytes of the elements before it. `[-1]` skips all
  elements except the last.
- After the target, it reads nothing.

A typed read of a scalar allocates nothing (test/allocation.cpp). A typed
read allocates only when it meets a tag 29, or when it enters a tag 24 from
a lazy. A view result allocates nothing either: the owner is a copy of a
`std::shared_ptr`.

A binding allocates for its own values. `query` allocates the nodelist. A
filter evaluates an absolute query (`$...` inside the filter) once for the
whole query, not once for each child.

A held lazy is a start point. A path from a lazy at `$.cars` does not read
the bytes before `cars` again. Many reads below the same node are cheaper
from a lazy at that node.

A key that is not a text compares by value. A key that is a map or an array
costs more to compare (doc/complexity.md).

## 11. Threads

A `cbor::lazy` belongs to one thread, and every `owning_ref` is bound to one thread too. `cbor::is_thread_bound_v<T>`
says it in the type. `cbor::transfer` hands a lazy that nothing else shares
to another thread:

```cpp
std::jthread([](cbor::sendable<cbor::lazy> const &v) {
    cbor::at_path<"$[1]", std::int64_t>(v.value);
}, cbor::transfer(std::move(l)));
```

`at_path<Path, T>(encoded)` with bytes has no shared state. Many threads can
read the same bytes at the same time. A debug build throws
`std::logic_error` when a thread that does not own a lazy changes its marks.

## 12. What RFC 9535 has and this library does not have

The run-time `query` passes every case of the JSONPath Compliance Test
Suite (test/cts.cpp): 706 of 706 as expected. 50 of these are valid paths
with `match()` or `search()`, and the library refuses them as
`invalid_path`.

| RFC 9535 | Here |
|---|---|
| `match()`, `search()` (2.4.6, 2.4.7) and I-Regexp | not supported: `invalid_path` |
| Function extensions beyond the five of 2.4 | not supported: `invalid_path` |
| Normalized paths (2.7), the location of a node | not given: `query` gives values, not locations |

What this library adds to RFC 9535: an index selector on a map finds an
integer key, and a compile-time path takes EDN literals as keys and in
filters.
