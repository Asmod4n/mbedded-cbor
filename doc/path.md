# Paths

A path selects a part of a CBOR data item and reads it without a decode of
the whole item. The syntax is JSONPath, RFC 9535. Inside brackets and in a
filter, a path also takes a literal of CBOR diagnostic notation, EDN
(draft-ietf-cbor-edn-literals-28), so a key can be any CBOR item. A path at
compile time and a path at run time read the same grammar.

Each claim in this document has a test in `test/doc_path.cpp`. The test
names start with `doc path:`. A claim about allocations has its test in
`test/allocation.cpp`.

## Contents

1. The forms
2. Which types a typed read gives
3. Path syntax
4. Comparisons
5. Functions
6. CBOR beyond JSON: keys, tags, typed arrays
7. Singular queries
8. Compile time and run time
9. Errors
10. What the bytes form checks
11. Limits
12. Cost
13. Threads
14. RFC 9535 and this library

## 1. The forms

| Form | Path | Result |
|---|---|---|
| `at_path<Path, T>(encoded)` | compile time, singular | `std::expected<T, error>`, T a scalar |
| `at_path<Path, T>(owner, encoded)` | compile time, singular | `std::expected<owning_ref<T>, error>`, T a view |
| `at_path<Path, T>(lazy)` | compile time, singular | T a scalar: `std::expected<T, error>`. T a view: `std::expected<owning_ref<T>, error>` |
| `at_path<Path>(binding, lazy)` | compile time, singular | `std::expected<Binding::value, error>` |
| `at_path(binding, path, lazy)` | run time, singular | `std::expected<Binding::value, error>` |
| `query<Path>(binding, lazy)` | compile time, any | `std::expected<Binding::value, error>`, the value is an array |
| `query(binding, path, lazy)` | run time, any | `std::expected<Binding::value, error>`, the value is an array |
| `schema<T>::at_path<Path>(encoded, index...)` | compile time, over the members of T | `std::expected<X, error>`, X the type of the member |

Every function is in the namespace `cbor`. Bad input gives an error in the
`std::expected`. A path is a string literal for the compile-time forms and
a `std::string_view` for the run-time forms. A compile-time path that is not
valid leaves no form to call: the compiler reports that no function
matches.

A wrong use that the compiler cannot see throws `std::logic_error`:

- an empty owner in `at_path<Path, T>(owner, encoded)` and in
  `schema<T>::path(owner, encoded)`;
- a `cbor::lazy{}` that holds no item, in every form that takes a lazy.

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

An `owning_ref` compares and reaches its value as `std::optional` does:
`*name == "ann"` and `name->size()` read the view, `name == "ann"`
compares it, and two `owning_ref` compare their values. `*` and `->` refuse
a temporary `owning_ref`, so a view cannot outlive its owner in one
expression. `==` takes a temporary, because it gives a `bool`.
`owning_ref<cbor::typed_array>` has no `==`, as `cbor::typed_array` has none.

`T` is one of `std::string_view`, `std::span<std::byte const>` and
`cbor::typed_array`. The owner is a `std::shared_ptr<void const>`. A view
type without an owner does not compile. A temporary `std::string` or a
moved `std::string` beside the owner does not compile: that overload is
deleted.

The function does not check that the owner holds the bytes
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
the result holds the top-level item of the lazy.

### Any value into your own type

A binding is your type that builds values. The concept `cbor::binding`
requires a member type `value`. The reader of `decode.hpp` states what else
a binding gives for each kind of item. `query` also calls
`array_decode(count)` and `array_append(array, value)` to build the array of
the nodelist. Every form takes the binding as an lvalue.

With a binding, the result can be any CBOR item: a map, an array, a tag.

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
struct [[=cbor::tag(1702)]] log { std::vector<std::uint16_t> readings; };

auto const bytes = *cbor::schema<car>::encode(car{"kia", {120, 1600}, {{90, 1200}, {75, 1000}}});
cbor::schema<car>::at_path<"$.motor.cc">(bytes);        // 1600
cbor::schema<car>::at_path<"$.spares[1].hp">(bytes);    // 75
cbor::schema<car>::at_path<"$.spares[].hp">(bytes, 0);  // 90, index at run time
cbor::schema<car>::at_path<"$.spares[].hp">(bytes, 2);  // index_out_of_bounds
cbor::schema<log>::at_path<"$.readings[1]">(log_bytes); // one element of a typed array
```

The path names members of `T`. An index is a number or `[]`. Each `[]` takes
one run-time index argument, in order. A path to a member that `T` does not
have does not compile. This path is not JSONPath: it has no wildcard, no
filter and no descendant segment.

The static form gives a value of a type that is trivially copyable. A path
to a text or to a member that is not trivially copyable, such as a
`std::vector` of structs, does not compile.

For a text or a part of the message, use the accessor:

```cpp
auto root = *cbor::schema<car>::path(owner, *owner);
root.at_path<"$.make">();                // std::expected<std::string_view, error>, "kia"
auto const motor = root.at_path<"$.motor">();
motor->at_path<"@.cc">();                // 1600, a path relative to the accessor
```

A path that starts with `@` starts at the node of the accessor. The
accessor reads a path only as an lvalue. Keep the result of `schema<T>::path` in a
variable while an accessor from it lives (doc/known-limits.md). `schema<T>::path(encoded)` with a
`std::string_view` copies the bytes into a new owner.

## 2. Which types a typed read gives

| T | CBOR item | Other items |
|---|---|---|
| a signed or unsigned integer type, also `char` | major type 0 or 1, tag 2 or 3 with a magnitude up to 64 bits | `incorrect_type`; out of range: `number_out_of_range` |
| `double` | a half, single or double float; an integer from -2^53 to 2^53 | `incorrect_type`; an integer outside: `number_out_of_range` |
| `bool` | `true`, `false` | `incorrect_type` |
| `std::nullptr_t` | `null` | `incorrect_type` |
| `cbor::simple_value` | any simple value, also `false`, `true` and `null` | `incorrect_type` |
| `std::string_view` | a text string | `incorrect_type` |
| `std::span<std::byte const>` | a byte string | `incorrect_type` |
| `cbor::typed_array` | a tag of RFC 8746 over a byte string | `incorrect_type` |

A typed read converts in one case only: a `double` takes an integer when
the double holds it exactly, that is from -2^53 to 2^53. `lazy::get<double>`
does the same. A float is not an integer:

```cpp
cbor::at_path<"$", std::uint8_t>(/* 256 */);           // number_out_of_range
cbor::at_path<"$", unsigned>(/* -1 */);                // number_out_of_range
cbor::at_path<"$", double>(/* 1 */);                   // 1.0
cbor::at_path<"$", double>(/* 9007199254740993 */);    // number_out_of_range
cbor::at_path<"$", std::int64_t>(/* 1.0 */);           // incorrect_type
cbor::at_path<"$", int>(/* 1(5) */);           // incorrect_type
```

A typed read passes a tag that the path does not know (section 6), so
`at_path<"$", int>` reads 5 from `1(5)` and `at_path<"$", std::string_view>`
reads the text of `32("x")`. Any other T does not compile, for example `float`,
`std::string` or a view without an owner. For any other result, use a
binding.

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

RFC 9535 2.5 also gives these shorthands:

| Path | Same as |
|---|---|
| `$.*` | `$[*]` |
| `$..*` | `$..[*]` |
| `$..[0]` | the descendant segment with a bracket |

The parts in detail:

- A name in dot form starts with a letter, `_` or a byte of 0x80 or more,
  and goes on with these or digits. A name in brackets is in single or
  double quotes, with the escapes of RFC 9535 2.3.1 and of EDN.
- An index is an integer of at most 16 digits without leading zeros, from
  -(2^53 - 1) to 2^53 - 1. An integer of 16 digits or less outside this
  range is `invalid_path`. `$[01]` is `invalid_path`.
- Any other item in brackets is an EDN literal, and the path reads it as a
  map key (section 6). So `$[-0]` is the key 0, and an integer of 17 digits
  or more is a key. On an array, such a key is `not_indexable`.
- A slice is `start:end:step`. Each part can be absent. Step 0 selects
  nothing. A slice of a node that is not an array selects nothing.
- `&&` binds more tightly than `||`. `!` negates an existence test or a
  group in parentheses.
- A literal in a filter is an EDN literal. A JSON number, a string,
  `true`, `false` and `null` are EDN literals too.
- Blanks are allowed before a segment and inside brackets. A blank before
  `$` or after the last segment is `invalid_path`.

A wildcard on a map selects the values. A name selects only from a map. An
index selects from an array, and from a map with an integer key (section
6).

## 4. Comparisons

A comparison is `==`, `!=`, `<`, `<=`, `>` or `>=`. Each side is a singular
query, a literal or a function. A side that is a query that is not singular
is `invalid_path`, for example `$[?@.* == 1]`.

| Values | `==` | `<` |
|---|---|---|
| two numbers | by value: 1 equals 1.0 | by value |
| two texts | by bytes | by bytes |
| two byte strings | by bytes | always false |
| two arrays | element by element, in order | always false |
| two maps | the same keys with equal values | always false |
| two tags | the same number and equal content | always false |
| a side that selects nothing | equal only to another side that selects nothing | always false |

A bignum (tag 2 or 3) is a tag, so it is not equal to an integer:
`$[?@ == 1]` does not select `2(h'01')`. Values of two different kinds are
not equal. `<=` is true when `<` or `==` is true, so two sides that select
nothing give true for `<=` and false for `<`.

## 5. Functions

| Function | Gives | RFC 9535 |
|---|---|---|
| `length(x)` | for a text, the count of its bytes that are not UTF-8 continuation bytes (0x80 to 0xBF); the elements of an array; the pairs of a map; Nothing for other values | 2.4.4 |
| `count(q)` | the number of nodes in the nodelist of q | 2.4.5 |
| `value(q)` | the value of the node when q has one node; Nothing otherwise | 2.4.8 |
| `match(x, re)` | not supported: `invalid_path` | 2.4.6 |
| `search(x, re)` | not supported: `invalid_path` | 2.4.7 |

Nothing is the result of a function that has no value (RFC 9535 2.4.1).
Nothing equals Nothing and no other value. For a text in valid UTF-8, the
`length` count is the count of characters. A byte string is not a text, so
its `length` is Nothing.

```cpp
// [{"t": "abc"}, {"t": [1, 2]}, {"t": 5}]
cbor::query(binding, "$[?length(@.t) == 3].t", l);    // ["abc"]
cbor::query(binding, "$[?count(@.t[*]) == 2].t", l);  // [[1, 2]]
cbor::query(binding, "$[?value(@..t) == 5].t", l);    // [5]
cbor::query(binding, "$[?match(@.t, 'a.c')]", l);     // invalid_path
```

The library has no regular expressions. A compile-time path with `match`
or `search` does not compile: `is_valid_path_v<"$[?match(@.t, 'a.c')]">` is
false. A function that is not well-typed (RFC 9535 2.4.3) is
`invalid_path`.

## 6. CBOR beyond JSON

### Keys that are not text

A map key in CBOR can be any item. Two ways reach such a key:

- An index selector on a map selects the value under the equal integer key.
  `$[1]` finds the key 1, and `$[-2]` finds the key -2. This is not an index
  from the end: a map has no order in a path.
- An EDN literal in brackets is a key: `$[h'01']` for a byte string,
  `$[[1, 2]]` for an array, `$[1.5]`, `$[true]`, `$[1000("x")]` for a tag,
  `$[{"a": 1}]` for a map. This holds at compile time and at run time.

```cpp
// {1: "x", -2: "y", h'01': "z", [1, 2]: "w"}
cbor::at_path<"$[1]">(binding, l);                   // "x"
cbor::at_path<"$[-2]">(binding, l);                  // "y"
cbor::at_path<"$[h'01']">(binding, l);               // "z"
cbor::at_path<"$[[1, 2]]">(binding, l);              // "w"
cbor::at_path(binding, "$[h'01']", l);               // "z", path at run time
cbor::at_path<"$[h'01']", std::string_view>(l);      // owning_ref, "z"
```

Keys compare by value (RFC 8949 5.6.1). So `h'01'` and `b64'AQ'` name the
same key. A key with a head that is not in preferred form is the same key.
An integer and a float are different keys: `$[1.0]` does not find the key
1.

When a map has the same key twice, every singular form takes the first
pair. A wildcard, a descendant segment and a filter take all pairs.

### Tags on the way

| Tag | What a path does |
|---|---|
| 24, encoded CBOR data item | goes into the byte string and reads the embedded item |
| 28, shareable | passes it; the value is the content |
| 29, shared reference | reads the value that the reference names |
| 55799, self-described CBOR | passes it, at the top, inside the item and inside tag 24 |
| 2 and 3, bignum | a typed read into an integer type reads the magnitude |
| an RFC 8746 typed array tag | an index reads one element; a typed read into `cbor::typed_array` reads the whole array |
| any other tag | passes it; the value is the content (RFC 8949 6.1) |

```cpp
cbor::at_path<"$.a[1]", int>(/* {"a": 24(<<[1, 2]>>)} */);          // 2
cbor::at_path<"$[1].k", int>(/* [28({"k": 7}), 29(0)] */);          // 7
cbor::at_path<"$.a", int>(/* 55799({"a": 5}) */);                   // 5
cbor::at_path<"$.a[1]", int>(/* {"a": 24(<<55799([1, 2])>>)} */);   // 2
cbor::at_path<"$.v[1]", int>(/* {"v": 1000([1, 2])} */);            // 2
cbor::query(binding, "$.v[*]", /* same */);                         // [1, 2]
cbor::at_path(binding, "$.v", /* same */);                          // 1000([1, 2])
cbor::at_path<"$", int>(/* 1(5) */);                                // 5
```

A path passes a tag that it does not know, as a generic decoder does (RFC
8949 6.1): a step and a typed read see the content. A path that ends at the
tag gives the whole tagged item to a binding, because a binding gets the tag
number. Tags 2 and 3 are not passed: a typed read into an integer reads the
bignum, and an index on a bignum is `not_indexable`. A map key behind tag 28
or tag 55799 is matched as the key without the tag. A key behind any other
tag is a different key: `$.k` does not find `1000("k")`.

Tag 29 can point back to a tag 28 anywhere before it. When a typed read
meets tag 29, it reads the item again through a lazy to find the marks.
This costs an allocation. A tag 29 that points forward is
`sharedref_index_not_marked`.

### Typed arrays

A typed array (RFC 8746) is a tag over a byte string. The tag gives the
type, the sign and the byte order of the elements. An index reads one
element as an integer or a float, and a wildcard, a slice or a filter gives
every element. A path that ends at the tag reads the whole array.

```cpp
// {"v": 69(h'07000800')}, uint16 little endian [7, 8]
cbor::at_path<"$.v[1]", int>(bytes);                       // 8
cbor::at_path<"$.v[-1]", double>(bytes);                   // 8.0
cbor::at_path<"$.v[2]", int>(bytes);                       // index_out_of_bounds
cbor::at_path<"$.v[1][0]", int>(bytes);                    // not_indexable
cbor::query(binding, "$.v[*]", lazy);                      // [7, 8]
cbor::at_path<"$.v", cbor::typed_array>(lazy);             // tag 69, 4 bytes
```

An element of an integer array is an integer, and an element of a float
array is a float of the same width. An element of a 128-bit float array
(tags 83 and 87) is `incorrect_type`, because no C++ type here holds it.
The typed read over bytes allocates nothing. A binding, `query` and the
lazy walk read an element as a small CBOR item that the library writes, and
this allocates once for each step. The element of a clamped uint8 array
(tag 68) is an unsigned integer.

## 7. Singular queries

A singular query (RFC 9535 2.3.5.1) has only name selectors and index
selectors, one in each segment, and no descendant segment. It selects one
node at most. An EDN key in brackets is a name selector with one key, so a
path with it is singular. `at_path` reads only a singular query. `query`
reads every query.

```cpp
static_assert(cbor::is_singular_query_v<"$.a[0]['b']">);
static_assert(cbor::is_singular_query_v<"$[h'01']">);
static_assert(!cbor::is_singular_query_v<"$.a[*]">);
static_assert(!cbor::is_singular_query_v<"$..a">);
static_assert(cbor::is_valid_path_v<"$..a">);
static_assert(!cbor::is_valid_path_v<"$.a[">);
```

- `is_valid_path_v<Path>` is true when the grammar accepts the path.
- `is_singular_query_v<Path>` is true when the path is valid and singular.

`at_path<Path, T>` with a path that is not singular does not compile.
`at_path<Path>(binding, lazy)` with such a path stops at a `static_assert`
that names `cbor::query`. `at_path(binding, path, lazy)` with such a path
gives `invalid_path`.

## 8. Compile time and run time

| Check | Compile-time path | Run-time path |
|---|---|---|
| Grammar of RFC 9535 with EDN literals | at compile time | at each call: `invalid_path` |
| Singular path for `at_path` | at compile time | at each call: `invalid_path` |
| Segments against the nesting depth | default depth at compile time; depth in force at each call | depth in force at each call |
| Type T of a typed read | at compile time | no typed run-time form |
| Everything about the data | at each call | at each call |

A compile-time path is parsed once, by the compiler. At run time it is a set
of constant arrays. A run-time path is parsed at each call, and the parse
allocates. To read the same run-time path many times, keep it short or use
a compile-time path.

## 9. Errors

Every error is a `cbor::error` in a `std::expected`. `doc/basics.md`
states the error values, `std::error_code` and the wrong uses that throw.
This table states when a path gives each error.

| Error | When |
|---|---|
| `invalid_path` | The path text is not valid. A run-time `at_path` with a path that is not singular. `match` or `search`. A comparison with a side that is not singular. An index of 16 digits or less outside ±(2^53 - 1). |
| `key_not_found` | A name or a key is not in the map. |
| `index_out_of_bounds` | The index is outside the array, also a run-time index of `schema<T>::at_path`. |
| `not_indexable` | A name, an index or a key meets a value that is not a map or an array, for example a text, a number, a bignum or an element of a typed array. A name or a key meets an array or a typed array. |
| `incorrect_type` | A typed read finds another kind of item than T takes. |
| `number_out_of_range` | The integer does not fit T. |
| `nesting_depth_exceeded` | Section 11. |
| `string_length_exceeded`, `container_elements_exceeded`, `input_bytes_exceeded` | A limit is in force and the input goes past it (section 11). |
| `too_little_data` | The bytes end before the target or before an item that the walk reads. |
| `syntax_error`, `indefinite_length` | A head that the walk reads is not well-formed, or uses indefinite length. |
| `sharedref_index_not_marked`, `sharedref_not_complete` | A tag 29 names no tag 28 before it, or names a tag 28 that contains it. |
| `inadmissible_type_for_tag_content` | A tag on the way has content that its definition does not allow, for example tag 24 over a number. |
| `nodelist_too_long` | One step of `query` selects more nodes than the top-level item has bytes. Tag 29 makes this possible. |

```cpp
// {"a": [1, 2], "s": "x"}
cbor::at_path(binding, "$.a[", l);                // invalid_path
cbor::at_path<"$.b", int>(bytes);                 // key_not_found
cbor::at_path<"$.a[2]", int>(bytes);              // index_out_of_bounds
cbor::at_path<"$.s[0]", int>(bytes);              // not_indexable
cbor::at_path<"$.s", int>(bytes);                 // incorrect_type
cbor::at_path<"$.a[1]", int>(bytes.substr(0, 5)); // too_little_data
cbor::at_path(binding, "$", /* 28([29(0)]) */);   // sharedref_not_complete
cbor::query(binding, "$[*][*]", /* [28([0, 0, 0, 0, 0, 0, 0, 0]), 29(0)] */); // nodelist_too_long
```

`query` gives no error when a selector finds nothing: the nodelist is
empty. The errors above are for a singular path, and for bytes that are not
valid in any form.

## 10. What the bytes form checks

A path reads the heads from the start of the item to the target, and the
heads and strings that it skips on the way. It does not read the bytes
after the target. So these errors are not found:

```cpp
cbor::at_path<"$[0]", int>(/* 82 01, the second element is missing */);  // 1
cbor::at_path<"$[0]", int>(/* 82 01 ff */);                             // 1
cbor::at_path<"$", int>(/* 01 02, a byte after the item */);             // 1
cbor::at_path<"$[1]", int>(/* 82 01 */);                                // too_little_data
```

`lazy::from` does not read the whole item either: it accepts `82 01` and
`01 02`. To check a whole item, decode it.

## 11. Limits

`doc/basics.md` states `cbor::limits`, the defaults, the bounds and the
macros. A path reads the limits once at each call.

| Limit | Default | What a path does |
|---|---|---|
| `nesting_depth` | 128, bound 512 | See below. |
| `string_length` | `SIZE_MAX` | Every string that the walk reads is checked: a key it compares and the target. A string that it only skips does not count. |
| `container_elements` | `SIZE_MAX` | Every array and map that the walk reads is checked: the containers on the way and the target. A container that it only skips does not count. |
| `input_bytes` | `SIZE_MAX` | The typed forms over bytes and `lazy::from` check the size of the bytes they get. A read from a lazy that exists does not check it again. |

`nesting_depth_exceeded` comes when:

- the path has more segments than the depth;
- a filter, a group in parentheses, a function call or a literal in the
  path is nested deeper than the depth;
- a descendant segment goes deeper than the depth;
- a binding decodes a result that is deeper than the depth. The binding
  counts the depth from the selected node.

An item that a typed read only skips can be deeper than the depth: the skip
keeps a count and no stack. A compile-time path has at most the default
nesting depth of segments, or it does not compile.

```cpp
cbor::limits = {.string_length = 2};
cbor::at_path<"$.n", int>(/* {"long": "xyz", "n": 1} */);   // string_length_exceeded, the key "long" is read
cbor::at_path<"$.n", int>(/* {"a": "xyz", "n": 1} */);      // 1, "xyz" is only skipped
cbor::at_path<"$.a", std::string_view>(/* same */);         // string_length_exceeded
```

The limits count only what a path or a lazy reads or decodes. An item that
it only skips does not count, and this holds for the iterators of
`elements()` and `entries()` too. A skip stays safe: it reads heads only, it
jumps over the content of a string, and it stops with `too_little_data` when
the item claims more than the bytes that are left. `item_size`, `decode`
and `lazy_decode` read every head, so every head counts there.

## 12. Cost

A path reads the heads from the start of the node to the target. It does
not decode what it passes:

- In a map, it reads each key until the key matches, and skips each value
  before. A skip reads heads only and jumps over the content of strings.
- In an array, `[i]` skips the i elements before the target. So an index is
  linear in the bytes of the elements before it. `[-1]` skips all
  elements except the last.
- After the target, it reads nothing.

A typed read of a scalar allocates nothing, also through tag 24. A typed
read allocates when it meets a tag 29, and the form over bytes allocates
when a key is not a text: these two cases read the item through a lazy. A
view result allocates nothing: the owner is a copy of a
`std::shared_ptr`.

A binding allocates for its own values. `query` allocates the nodelist. A
filter with a literal allocates for each child that it tests.

A held lazy is a start point. A path from a lazy at `$.cars` does not read
the bytes before `cars` again. Many reads below the same node are cheaper
from a lazy at that node.

A key that is not a text compares by value. A key that is a map or an array
costs more to compare (doc/complexity.md).

## 13. Threads

`doc/basics.md` states the threads: a `cbor::lazy` and every
`owning_ref` belong to one thread, and `cbor::transfer` hands a lazy to
another thread. A path from a lazy follows these rules.

`at_path<Path, T>(encoded)` with bytes has no shared state. Many threads can
read the same bytes at the same time.

## 14. RFC 9535 and this library

The run-time `query` gives the result of the JSONPath Compliance Test Suite
(test/cts.cpp) in 694 of 706 cases. 50 of these are valid paths with
`match()` or `search()`, and the library refuses them as `invalid_path`.

The other 12 cases are invalid in RFC 9535 and valid EDN, so a path accepts
them:

| Path | EDN reads |
|---|---|
| `$[?@.a==+1]`, `$[?@.a==.1]`, `$[?@.a==-.1]`, `$[?@.a==1.]`, `$[?@.a==1.e1]` | a number |
| `$[1.0]`, `$[+1]`, `$[-0]` | a key: 1.0, 1, 0 |
| `$["\'"]`, `$['\"']` | a name with a quote |
| `$["\u{1234}"]`, `$["\u{10ffff}"]` | a name with that character |

| RFC 9535 | Here |
|---|---|
| `match()`, `search()` (2.4.6, 2.4.7) and I-Regexp | not supported: `invalid_path` |
| Function extensions beyond the five of 2.4 | not supported: `invalid_path` |
| Normalized paths (2.7), the location of a node | not given: `query` gives values, not locations |

What this library adds to RFC 9535: an index selector on a map finds an
integer key, and a path takes EDN literals as keys and in filters.
