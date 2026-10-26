# Paths in detail

This page adds the details to [../path.md](../path.md). A path in brackets
and in a filter also takes a literal of CBOR diagnostic notation, EDN
(draft-ietf-cbor-edn-literals-28), so a key can be any CBOR item. A path
at compile time and a path at run time read the same grammar.

Every function is in the namespace `cbor`. A comment shows the CBOR in
`bytes` as text. In the examples, `l` is a `cbor::lazy` over `bytes`.

## 1. The forms

| Form | Path | Result |
|---|---|---|
| `at_path<Path, T>(encoded)` | compile time, singular | `std::expected<T, error>`, T a scalar |
| `at_path<Path, T>(encoded)` | compile time, singular | `std::expected<T, error>`, T `std::string` or `std::vector<std::byte>`: a copy of the text or of the bytes of the value |
| `at_path<Path, T>(owner)` | compile time, singular | `std::expected<ref<T>, error>`, T a view, `owner` a `std::shared_ptr<std::string const>`, read in place |
| `at_path<Path, T>(owner, view)` | compile time, singular | `std::expected<ref<T>, error>`, T a view, `owner` a `std::shared_ptr<void const>`, `view` a `std::string_view` of bytes that `owner` keeps alive |
| `at_path<Path, T>(std::move(encoded))` | compile time, singular | `std::expected<ref<T>, error>`, T a view; the rvalue `std::string` is moved into a new owner |
| `lazy.at_path<Path, T>()` | compile time, singular | T a scalar: `std::expected<T, error>`. T a view: `std::expected<ref<T>, error>` |
| `lazy.at_path<Path>()` | compile time, singular | `std::expected<lazy, error>`, the node |
| `lazy.at_path<Path, nodelist>()` | compile time, any | `std::expected<nodelist, error>`, the nodelist |
| `jsonpath::compile<T>(path)` | run time | `std::expected<jsonpath::query<T>, error>`, the parsed path |
| `lazy.at_path(e)` | run time, `e` a query | T `lazy`: the node. T `nodelist`: the nodelist. Else as `lazy.at_path<Path, T>()` |
| `at_path(encoded, e)` | run time, `e` a query | T a scalar: as `at_path<Path, T>(encoded)` |
| `at_path(owner, view, e)`, `at_path(owner, e)`, `at_path(std::move(encoded), e)` | run time, `e` a query | T a view: `std::expected<ref<T>, error>`. T `lazy`: the node. T `nodelist`: the nodelist |

No free form takes a lazy. A lazy reads a path with its member
`lazy.at_path`.

`encoded` is a `std::string_view`, so a `std::string` or one element of a
`cbor::sequence` goes in directly. A path is a string literal for the
compile-time forms. A run-time path is a `std::string_view` that
`jsonpath::compile<T>` parses once. The query owns its data
and is applied to any number of items. `T` of a `compile` that is
not `nodelist` needs a singular path, else the result is
`not_singular_query`. `nodelist` is `std::vector<lazy>`. A
compile-time path that is not valid leaves no form to call: the compiler
reports that no function matches. `schema<T>::at_path` reads a member of
a packed message ([schema.md](schema.md)).

A wrong use that the compiler cannot see throws `std::logic_error`:

- a null or empty owner in a form that takes an owner;
- a `cbor::lazy{}` that holds no item, in every form that takes a lazy.

### A view and its owner

`T` of a view is one of `std::string_view`, `std::span<std::byte const>`
and `cbor::typed_array`. A view type needs an owner of the bytes. A view
type with a `std::string_view` alone does not compile. No form copies the
input bytes: the form with an rvalue `std::string` moves them.

The owner beside a view is a `std::shared_ptr<void const>`. The view is a
borrowed range of `char`, for example a `std::string_view`. The function
does not check that the owner holds the bytes. A `std::string` beside the
owner does not compile, because it is not a borrowed range.

A `ref` compares and reaches its value as `std::optional` does:
`*name == "ann"` and `name->size()` read the view, `name == "ann"`
compares it, and two `ref` compare their values.
`ref<cbor::typed_array>` has no `==`, as `cbor::typed_array` has none.

## 2. Any item as a node

The forms without a type give the node as a `cbor::lazy`. The node can be
any CBOR item: a map, an array, a tag. Read it on with a step, `get`,
`type()` or `clone()` ([runtime.md](runtime.md)).

```cpp
// {"a": [1, 2, 3]}
l.at_path<"$.a">();          // the node of [1, 2, 3]
l.at_path<"$.a[*]", cbor::nodelist>();   // the nodes of 1, 2 and 3
auto const e = cbor::jsonpath::compile<cbor::nodelist>("$.a[?@ > 1]");
l.at_path(*e);               // the nodes of 2 and 3, path at run time
l.at_path<"$.b", cbor::nodelist>();      // no node, no error
```

`at_path` with a node type gives one node. `at_path` with `nodelist`
gives the nodelist, also for a singular path and for an empty nodelist.
It gives no error when a selector finds nothing.

## 3. Which types a typed read gives

| T | CBOR item | Other items |
|---|---|---|
| a signed or unsigned integer type, also `char` | an integer, or a tag 2 or 3 with a magnitude up to 64 bits | `incorrect_type`; out of range: `number_out_of_range` |
| `double` | a half, single or double float; an integer from -2^53 to 2^53 | `incorrect_type`; an integer outside: `number_out_of_range` |
| `bool` | `true`, `false` | `incorrect_type` |
| `std::nullptr_t` | `null` | `incorrect_type` |
| `cbor::simple_value` | any simple value, also `false`, `true` and `null` | `incorrect_type` |
| `std::string_view` | a text string | `incorrect_type` |
| `std::span<std::byte const>` | a byte string | `incorrect_type` |
| `cbor::typed_array` | a tag of RFC 8746 over a byte string | `incorrect_type` |

A typed read converts in one case only: a `double` takes an integer when
the double holds it exactly, that is from -2^53 to 2^53. `lazy::get<double>`
does the same. A float is not an integer.

| bytes | Call | Result |
|---|---|---|
| `-1` | `at_path<"$", unsigned>` | `number_out_of_range` |
| `9007199254740993` | `at_path<"$", double>` | `number_out_of_range` |
| `1.0` | `at_path<"$", std::int64_t>` | `incorrect_type` |
| `2(h'0100')` | `at_path<"$", std::uint64_t>` | `256` |
| `1(5)` | `at_path<"$", int>` | `5` |

A typed read passes a tag that the path does not read (section 7), so
`at_path<"$", int>` reads 5 from `1(5)` and
`at_path<"$", std::string_view>` reads the text of `32("x")`. The free
form over bytes also takes `std::string` and `std::vector<std::byte>`, and
copies the value (section 1). Any other T does not compile, for example
`float` or a view without an owner. For any other result, read the node of
`lazy.at_path<Path>()`.

## 4. Path syntax

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
- An index counts from 0. `[0]` is the first element and `[-1]` the last.
- An index is an integer of at most 16 digits without leading zeros, from
  -(2^53 - 1) to 2^53 - 1. An integer of 16 digits or less outside this
  range is `invalid_path`. `$[01]` is `invalid_path`.
- Any other item in brackets is an EDN literal, and the path reads it as a
  map key (section 7). So `$[-0]` is the key 0, and an integer of 17 digits
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
7).

## 5. Comparisons

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

## 6. Functions

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
l.at_path<"$[?length(@.t) == 3].t", cbor::nodelist>();    // the node of "abc"
l.at_path<"$[?count(@.t[*]) == 2].t", cbor::nodelist>();  // the node of [1, 2]
l.at_path<"$[?value(@..t) == 5].t", cbor::nodelist>();    // the node of 5
cbor::jsonpath::compile<cbor::nodelist>("$[?match(@.t, 'a.c')]");  // invalid_path
```

A compile-time path with `match` or `search` does not compile:
`is_valid_path_v<"$[?match(@.t, 'a.c')]">` is false. A function that is
not well-typed (RFC 9535 2.4.3) is `invalid_path`.

## 7. CBOR beyond JSON

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
l.at_path<"$[1]">();                   // the node of "x"
l.at_path<"$[-2]">();                  // the node of "y"
l.at_path<"$[h'01']">();               // the node of "z"
l.at_path<"$[[1, 2]]">();              // the node of "w"
l.at_path(*cbor::jsonpath::compile<cbor::lazy>("$[h'01']"));  // the node of "z", path at run time
l.at_path<"$[h'01']", std::string_view>();      // ref, "z"
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
| 29, shared reference | passes it as any other tag; the value is the content, the index. A path does not resolve the reference |
| 55799, self-described CBOR | passes it, at the top, inside the item and inside tag 24 |
| 2 and 3, bignum | a typed read into an integer type reads the magnitude |
| an RFC 8746 typed array tag | an index reads one element; a typed read into `cbor::typed_array` reads the whole array |
| any other tag | passes it; the value is the content (RFC 8949 6.1) |

| bytes | Call | Result |
|---|---|---|
| `{"a": 24(<<[1, 2]>>)}` | `at_path<"$.a[1]", int>` | `2` |
| `[28({"k": 7}), 29(0)]` | `at_path<"$[0].k", int>` | `7` |
| `[28({"k": 7}), 29(0)]` | `at_path<"$[1]", int>` | `0`, the index |
| `55799({"a": 5})` | `at_path<"$.a", int>` | `5` |
| `{"a": 24(<<55799([1, 2])>>)}` | `at_path<"$.a[1]", int>` | `2` |
| `{"v": 1000([1, 2])}` | `at_path<"$.v[1]", int>` | `2` |
| `{"v": 1000([1, 2])}` | `l.at_path<"$.v[*]", cbor::nodelist>()` | the nodes of `1` and `2` |
| `{"v": 1000([1, 2])}` | `l.at_path<"$.v">()` | the node of `1000([1, 2])` |

A path passes a tag that it does not read, as a generic decoder does (RFC
8949 6.1): a step and a typed read go to the content. A path that ends at the
tag gives the node of the whole tagged item, so the caller reads the tag
number. Tags 2 and 3 are not passed: a typed read into an integer reads the
bignum, and an index on a bignum is `not_indexable`. A map key behind tag 28
or tag 55799 is matched as the key without the tag. A key behind any other
tag is a different key: `$.k` does not find `1000("k")`.

A path does not resolve a tag 29. Only `cbor::structs` decode resolves
tags 28 and 29.

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
l.at_path<"$.v[*]", cbor::nodelist>();              // the nodes of 7 and 8
l.at_path<"$.v", cbor::typed_array>();                // tag 69, 4 bytes
```

An element of an integer array is an integer, and an element of a float
array is a float of the same width. An element of a 128-bit float array
(tags 83 and 87) is `incorrect_type`, because no C++ type here holds it.
The element of a clamped uint8 array (tag 68) is an unsigned integer.

## 8. Singular queries

A singular query (RFC 9535 2.3.5.1) has only name selectors and index
selectors, one in each segment, and no descendant segment. It selects one
node at most. An EDN key in brackets is a name selector with one key, so a
path with it is singular. `at_path` with a node or a typed result reads
only a singular query. `at_path` with `nodelist` reads every query.

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
`lazy.at_path<Path>()` with such a path stops at a `static_assert` that
names `cbor::nodelist`. `compile<T>(path)` with such a path and a
`T` that is not `nodelist` gives `not_singular_query`.

## 9. Compile time and run time

| Check | Compile-time path | Run-time path |
|---|---|---|
| Grammar of RFC 9535 with EDN literals | at compile time | once, in `compile`: `invalid_path` |
| Singular path for a result that is not `nodelist` | at compile time | once, in `compile`: `not_singular_query` |
| Segments against the nesting depth | at compile time | once, in `compile`: `nesting_depth_exceeded` |
| Type T of a typed read | at compile time | at compile time, the `T` of the query |
| Everything about the data | at each call | at each call |

`compile` parses a run-time path once, and the parse allocates.
An apply of the query does not parse again.

## 10. Errors

| Error | When |
|---|---|
| `invalid_path` | The path text is not valid. `match` or `search`. A comparison with a side that is not singular. An index of 16 digits or less outside ±(2^53 - 1). |
| `not_singular_query` | `compile<T>` with a path that is not singular and a `T` that is not `nodelist`. |
| `key_not_found` | A name or a key is not in the map. |
| `index_out_of_bounds` | The index is outside the array. |
| `not_indexable` | A name, an index or a key meets a value that is not a map or an array, for example a text, a number, a bignum or an element of a typed array. A name or a key meets an array or a typed array. |
| `incorrect_type` | A typed read finds another kind of item than T takes. |
| `number_out_of_range` | The integer does not fit T. |
| `nesting_depth_exceeded` | Section 12. |
| `string_length_exceeded`, `container_elements_exceeded` | A limit is set and the input goes past it (section 12). |
| `too_little_data` | The bytes end before the target or before an item that the walk reads. |
| `syntax_error`, `indefinite_length` | A head that the walk reads is not well-formed, or uses indefinite length. |
| `inadmissible_type_for_tag_content` | A tag on the way has content that its definition does not allow, for example tag 24 over a number. |
| `nodelist_too_long` | One step of a nodelist read selects more nodes than the top-level item has bytes. |

| bytes | Call | Result |
|---|---|---|
| `{"a": [1, 2], "s": "x"}` | `compile<cbor::lazy>("$.a[")` | `invalid_path` |
| the same | `at_path<"$.s[0]", int>(bytes)` | `not_indexable` |
| the first 5 bytes of the same | `at_path<"$.a[1]", int>` | `too_little_data` |
| `{"a": 24(5)}` | `at_path<"$.a", int>` | `inadmissible_type_for_tag_content` |

## 11. What the form over bytes checks

A path reads the heads from the start of the item to the target, and the
heads and strings that it skips on the way. Each data item has at least
one byte, so an array or a map that claims more items than the bytes left
gives `too_little_data` before any element is read. A path does not read
the bytes after the target. So these faults are not found:

| bytes | Call | Result |
|---|---|---|
| `82 01`, the second element is missing | `at_path<"$[0]", int>` | `too_little_data` |
| `82 01 ff` | `at_path<"$[0]", int>` | `1` |
| `01 02`, a byte after the item | `at_path<"$", int>` | `1` |
| `82 01` | `at_path<"$[1]", int>` | `too_little_data` |

`lazy::take_ownership` and `lazy::view` do not read the whole item
either: they accept `82 01` and `01 02`. A step into `82 01` gives `too_little_data`. `item_size` and a
decode check a whole item.

## 12. Limits

[basics.md](basics.md) states `cbor::limits`, the defaults, the bounds and
the macros. The limits are constants of the build.

| Limit | What a path does |
|---|---|
| `nesting_depth` | See below. |
| `string_length` | Every string that the walk reads is checked: a key it compares and the target. A string that it only skips does not count. |
| `container_elements` | Every array and map that the walk reads is checked: the containers on the way and the target. A container that it only skips does not count. |

`nesting_depth_exceeded` comes when:

- the path has more segments than the depth;
- a filter, a group in parentheses, a function call or a literal in the
  path is nested deeper than the depth;
- a descendant segment goes deeper than the depth;
- `clone()` of a node reads an item that is deeper than the depth. It
  counts the depth from the node.

An item that a typed read only skips can be deeper than the depth. A
compile-time path has at most the default nesting depth of segments, or it
does not compile.

```cpp
// built with -DCBOR_STRING_LENGTH_DEFAULT=2
// {"long": "xyz", "n": 1}
cbor::at_path<"$.n", int>(bytes);                       // string_length_exceeded, the key "long" is read
// {"a": "xyz", "n": 1}
cbor::at_path<"$.n", int>(bytes);                       // 1, "xyz" is only skipped
cbor::at_path<"$.a", std::string_view>(bytes);          // string_length_exceeded
```

The limits count only what a path or a lazy reads or decodes. This holds
for the iterators of `elements()` and `entries()` too. A skip stays safe:
it jumps over the content of a string, and it stops with
`too_little_data` when the item claims more than the bytes that are left.

## 13. Cost

A path reads the heads from the start of the node to the target. It does
not decode what it passes:

- In a map, it reads each key until the key matches, and skips each value
  before. A skip jumps over the content of strings.
- In an array, `[i]` skips the i elements before the target. So an index is
  linear in the bytes of the elements before it. `[-1]` skips all
  elements except the last.
- After the target, it reads nothing.

A typed read of a scalar allocates nothing, also through tag 24 and
with a key that is not a text. A
view result from an owner allocates nothing: the owner in the result is
a copy of a `std::shared_ptr`. The form with an rvalue `std::string`
allocates the `std::shared_ptr` that holds the string, once.

A `nodelist` read allocates the nodelist. A filter with a literal allocates for each
child that it tests. `at_path` and the lazy walk read an element
of a typed array as a small CBOR item that the library writes, and this
allocates once for each step.

A held lazy is a start point. A path from a lazy at `$.cars` does not read
the bytes before `cars` again. Many reads below the same node are cheaper
from a lazy at that node.

A key that is not a text compares by value. A key that is a map or an array
costs more to compare ([complexity.md](complexity.md)).

## 14. Threads

A path from a lazy follows the rules of a lazy ([basics.md](basics.md)):
several threads read one `cbor::lazy` at the same time.
`at_path<Path, T>(encoded)` with bytes has no shared state.

## 15. RFC 9535 and this library

A run-time `nodelist` query gives the result of the JSONPath Compliance Test Suite
in 694 of 706 cases. 50 of these are valid paths with `match()` or
`search()`, and the library refuses them as `invalid_path`.

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
| Normalized paths (2.7), the location of a node | not given: a `nodelist` gives values, not locations |

What this library adds to RFC 9535: an index selector on a map finds an
integer key, and a path takes EDN literals as keys and in filters.
