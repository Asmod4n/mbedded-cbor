# Basics in detail

This page adds the details to [../basics.md](../basics.md): the error
codes, every error value, the limits, the nesting depth, memory, threads
and owners.

## 1. Error codes

`cbor::error` is an error code enum: `std::is_error_code_enum` is true for
it. A `cbor::error` converts to a `std::error_code` with the category
`cbor`. `message()` gives the text of the error.

```cpp
std::error_code const e = cbor::error::key_not_found;
e.message();          // "key not found"
e.category().name();  // "cbor"
```

Four errors compare equal to a `std::errc`. No other error compares equal
to a `std::errc`. The comparison needs a `std::error_code` on the left: a
`cbor::error` and a `std::errc` do not compare directly, and that does not
compile.

| `cbor::error` | `std::errc` |
|---|---|
| `no_buffer_space` | `no_buffer_space` |
| `value_too_large` | `value_too_large` |
| `not_enough_memory` | `not_enough_memory` |
| `io_error` | `io_error` |

```cpp
std::error_code{cbor::error::no_buffer_space} == std::errc::no_buffer_space;  // true
```

## 2. Wrong use

A wrong use of the library does not compile where the compiler can see
it. A temporary `std::string` as the bytes of a view is one example. Where
the compiler cannot see a wrong use, the library throws
`std::logic_error`:

- an empty owner, or a null `std::shared_ptr<std::string const>`;
- a `cbor::lazy{}` that holds no item.

## 3. Every error value

| Error | When |
|---|---|
| `too_little_data` | The bytes end before the item ends. |
| `syntax_error` | A head is not well-formed, or a simple value is reserved in the two-byte form. No decode reads the bytes after its item. |
| `indefinite_length` | A head uses indefinite length. The library does not read indefinite length. `diagnostic_notation` prints it. |
| `nesting_depth_exceeded` | The item is deeper than the nesting depth (section 5). |
| `inadmissible_type_for_tag_content` | A tag has content that its definition does not allow. |
| `sharedref_index_not_marked` | `structs` decode meets a tag 29 that names no tag 28 before it. |
| `sharedref_index_out_of_range` | `structs` decode meets a tag 29 whose index does not fit `std::size_t`. |
| `sharedref_not_complete` | `structs` decode meets a tag 29 that names a tag 28 in a skipped member. |
| `reserved_simple_value` | An encode meets a simple value from 24 to 31. |
| `unsupported_value` | The encoder meets a typed array with a reserved tag or a tag outside RFC 8746. `structs` gives it for a map key that is an array, a map or a tag. |
| `not_indexable` | A key or an index meets an item that is not a map or an array. |
| `index_out_of_bounds` | The index is outside the array. |
| `key_not_found` | The key is not in the map. |
| `invalid_path` | The path text is not valid ([path.md](path.md)). |
| `incorrect_type` | A typed read finds another kind of item than T takes. |
| `number_out_of_range` | The number does not fit T. |
| `cyclic_data_structure` | No call of the library gives it. A writer of the program can give it. |
| `unpopulated_table_index` | A reference in a `schema<T>` message names an entry that its table does not have. |
| `nodelist_too_long` | One step of a query selects more nodes than the item has bytes. |
| `duplicate_key` | A deterministic check finds a key twice in one map. |
| `no_buffer_space`, `value_too_large`, `not_enough_memory`, `io_error` | The writer of an encode gives this error. `not_enough_memory` also comes from a refused allocation in `structs` and `schema` decode. |
| `string_length_exceeded`, `container_elements_exceeded` | A limit is set and the input goes past it (section 4). |
| `not_singular_query` | `jsonpath::compile<T>` gets a path that is not singular, and `T` is not `nodelist` ([path.md](path.md), section 10). |

```cpp
// owner: a std::shared_ptr<std::string const> with the bytes in the comment above each read
// [1, 2] without its last byte
cbor::lazy::view(owner)->at(1).error();                  // too_little_data
// [_ 1]
cbor::lazy::view(owner)->at(0).error();                  // indefinite_length
// {"a": 1}
cbor::lazy::view(owner)->at("b").error();                // key_not_found
// 1
cbor::lazy::view(owner)->at(0).error();                  // not_indexable
// "a"
cbor::lazy::view(owner)->get<std::int64_t>().error();    // incorrect_type
// 256
cbor::lazy::view(owner)->get<std::uint8_t>().error();    // number_out_of_range
```

## 4. Limits

The limits are constants of the build. A macro sets each limit, and the
library sets each macro that the build does not set. `cbor::limits` gives
the values. No call reads a limit at run time, and a check whose limit is
not set is not in the program.

| Macro | Constant | Default | Bound | Error |
|---|---|---|---|---|
| `CBOR_NESTING_DEPTH_DEFAULT` | `cbor::limits::nesting_depth` | 128 | 512 | `nesting_depth_exceeded` |
| `CBOR_STRING_LENGTH_DEFAULT` | `cbor::limits::string_length` | `SIZE_MAX` | `SIZE_MAX` | `string_length_exceeded` |
| `CBOR_CONTAINER_ELEMENTS_DEFAULT` | `cbor::limits::container_elements` | `SIZE_MAX` | `SIZE_MAX` | `container_elements_exceeded` |

```sh
g++ -DCBOR_NESTING_DEPTH_DEFAULT=32 -DCBOR_STRING_LENGTH_DEFAULT=4096 ...
```

```cpp
std::size_t const depth = cbor::limits::nesting_depth;   // 32
```

A value above the bound does not compile. All translation units of one
program use the same values: a different value is a violation of the one
definition rule.

The library does not bound the size of the input. The program bounds it
before it calls the library.

A limit is checked by the step that reads the item. `lazy::take_ownership`
and `lazy::view` check no limit. So a string that is too long gives
`string_length_exceeded` at `get`, not when the lazy is made.

The limits count what a call reads or decodes. An item that a path or a
lazy only skips does not count. `item_size`, `decode` and
`diagnostic_notation` read every item, so every item counts there.

```cpp
// owner: a std::shared_ptr<std::string const> with the bytes in the comment above each read
// built with -DCBOR_STRING_LENGTH_DEFAULT=2
// {"a": "xyz", "n": 1}
cbor::lazy::view(owner)->at("n")->get<std::int64_t>();        // 1: "xyz" is only skipped
cbor::lazy::view(owner)->at("a")->get<std::string_view>();    // string_length_exceeded
cbor::item_size(*owner);                                       // string_length_exceeded
```

## 5. Nesting depth

The nesting depth counts the arrays, maps and tags around an item. The
top-level item has the depth 0. An item deeper than `nesting_depth` gives
`nesting_depth_exceeded` where a call recurses: `decode`, `encode` of an
item, `diagnostic_notation`, a path and a filter.

An item that a call only skips can be deeper than the limit, and so can
the item of `item_size`: neither keeps a stack.

```cpp
// built with -DCBOR_NESTING_DEPTH_DEFAULT=2
// [[1]]
cbor::diagnostic_notation(bytes);   // "[[1]]"
// [[[1]]]
cbor::diagnostic_notation(bytes);   // nesting_depth_exceeded
cbor::item_size(bytes);             // 4
```

## 6. Memory

The caller sets the bound of the memory of a decode. `structs<T>::decode`
and `schema<T>::decode` take a target as their third argument. The target
holds `std::pmr` containers with a `std::pmr::memory_resource`. The decode
reads into the target and makes each element with the allocator of its
container. When the resource refuses, the decode returns
`error::not_enough_memory`. A `std::bad_alloc` does not leave
`structs<T>::decode` or `schema<T>::decode`.

```cpp
using texts = std::pmr::vector<std::pmr::string>;
auto const owner = std::make_shared<std::string const>(
    *cbor::structs<std::vector<std::string>>::encode({std::string(40, 'a'), std::string(40, 'b')}));
std::array<std::byte, 64> buffer{};
std::pmr::monotonic_buffer_resource budget(buffer.data(), buffer.size(), std::pmr::null_memory_resource());
cbor::structs<texts>::decode(owner, texts(&budget));   // not_enough_memory
```

The library takes no allocator in other places. These places allocate
with `operator new`, through the standard containers and
`std::make_shared`. A failed allocation there throws `std::bad_alloc`.
A plain struct inside a `std::pmr` container gets no allocator, so its
members use the default resource.

| Call | Allocates |
|---|---|
| `at_path<Path, T>(encoded)` for a scalar T | nothing |
| `item_size`, the iterator of `cbor::sequence` | nothing |
| a step of a lazy (`at`, `get`) | nothing |
| `lazy::view` | nothing |
| `lazy::take_ownership` | one allocation: the `std::shared_ptr<std::string const>` that holds the bytes |
| `lazy::clone` | its items |
| `diagnostic_notation`, the encoder into a `std::string` | the string |
| the encoder into a span | nothing |

The target of the encoder chooses the memory of the bytes
([runtime.md](runtime.md)).

## 7. Threads

The library starts no thread and holds no lock. It has no global that
changes: the limits are constants.

A call over bytes, for example `at_path<Path, T>(encoded)` and
`item_size`, has no shared state. Two lazy values from two calls of
`lazy::take_ownership` or `lazy::view` share nothing, also over the same
bytes.

A lazy holds only its owner, its bytes and its offset, and a read changes
nothing in it. So several threads read one lazy, and the lazy values of
one top-level item, at the same time. `lazy::take_ownership` gives a
`std::expected`,
and `std::move(*l)` moves the lazy out of it:

```cpp
// [1, 2]
auto l = cbor::lazy::take_ownership(std::move(bytes));
std::jthread([](cbor::lazy const &v) {
    v.at(1)->get<std::int64_t>();   // 2
}, std::move(*l));
```

## 8. Owners

A `cbor::lazy` holds the owner of the bytes. So does every lazy that a
step gives, and every iterator. A lazy from `at` stays valid after the
lazy that gave it and after the `std::string` that went into
`lazy::take_ownership` end.

```cpp
std::optional<cbor::lazy> keep;
{
    std::string bytes = message();   // {"tags": ["a", "b"]}
    keep = *cbor::lazy::take_ownership(std::move(bytes))->at("tags");
}
keep->at(1)->get<std::string_view>();   // "b"
```

A text or a byte string comes back in a `cbor::ref<T>`. The `ref` holds
the owner of the bytes.

- `*r` gives the value. A view comes back by value, any other `T` as
  `T const &`. `r->` reaches the members of the value.
- `*` and `->` do not compile on a temporary `ref`.
- `==` compares the value with a `T` or with another `ref`, as
  `std::optional` does. `==` takes a temporary, because it gives a `bool`.
- A copy of a `ref` shares the owner. It does not copy the bytes.
- `r.clone()` copies the value out: a `std::string` for a
  `std::string_view`, a `std::vector<std::byte>` for a
  `std::span<std::byte const>`, and a `cbor::owning<T>` for any other `T`.

The owner of the bytes comes from one of these:

| Call | Owner |
|---|---|
| `lazy::take_ownership(std::string&&)` | the library moves the string into a new `std::shared_ptr<std::string const>` |
| `lazy::view(std::shared_ptr<std::string const>)` | the caller |
| `lazy::view(owner, view)` | the caller: `owner` keeps the bytes of `view` alive |

A `std::string` lvalue and a `std::string_view` alone do not compile,
because no form copies the bytes:

```cpp
auto const shared = std::make_shared<std::string const>(message());
auto const a = cbor::lazy::view(shared).value();   // shares the bytes, no copy
auto const b = cbor::lazy::take_ownership(std::move(bytes)).value();   // moves the string, no copy
```

`structs<T>::decode`, `schema<T>::decode`, `schema<T>::path` and
`at_path` for a view result ([path.md](path.md)) take the same forms.
`cbor::sequence` takes the two forms with a `std::shared_ptr`, and no
rvalue `std::string`. The library takes no other pointer to bytes.

A larger object can hold the bytes. Two forms read them in place. The
aliasing constructor of `std::shared_ptr` makes an owner of the string
from the object. Or the object is the owner, and a view of its bytes
goes beside it.

```cpp
struct page {
    std::string bytes;   // "xyz"
};
auto const p = std::make_shared<page const>(message());
std::shared_ptr<std::string const> const owner(p, &p->bytes);   // shares the count of p
cbor::lazy::view(owner)->get<std::string_view>();               // "xyz", a view into p->bytes
cbor::lazy::view(p, std::string_view(p->bytes))->get<std::string_view>();   // the same view
```

An empty owner throws `std::logic_error`. A `std::string_view` beside an
owner compiles, and the library does not check that the owner holds its
bytes. A `std::string` beside an owner does not compile, because it is
not a borrowed range: its bytes can end before the owner ends.
