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
- a `cbor::lazy{}` that holds no item;
- a limit above its bound (section 4);
- `cbor::transfer` of a lazy that something else shares (section 7);
- in a debug build, a lazy in a thread that does not own it (section 7).

## 3. Every error value

| Error | When |
|---|---|
| `too_little_data` | The bytes end before the item ends. |
| `syntax_error` | A head is not well-formed, or a simple value is reserved in the two-byte form. `structs` also gives it for bytes after the item. |
| `indefinite_length` | A head uses indefinite length. The library does not read indefinite length. `diagnostic_notation` prints it. |
| `nesting_depth_exceeded` | The item is deeper than the nesting depth (section 5). |
| `inadmissible_type_for_tag_content` | A tag has content that its definition does not allow. |
| `sharedref_index_not_marked` | A tag 29 names no tag 28 before it. |
| `sharedref_index_out_of_range` | The index of a tag 29 does not fit `std::size_t`. |
| `sharedref_not_complete` | A tag 29 names a tag 28 that contains it, and the binding does not take a cycle. |
| `reserved_simple_value` | An encode meets a simple value from 24 to 31. |
| `unsupported_value` | An encode meets a value that the binding gives as `kind::unsupported`, or a typed array with a reserved tag. `structs` gives it for a map key that is an array, a map or a tag. |
| `not_indexable` | A key or an index meets an item that is not a map or an array. |
| `index_out_of_bounds` | The index is outside the array. |
| `key_not_found` | The key is not in the map. |
| `invalid_path` | The path text is not valid ([path.md](path.md)). |
| `incorrect_type` | A typed read finds another kind of item than T takes. |
| `number_out_of_range` | The number does not fit T. |
| `cyclic_data_structure` | An encode without sharing meets a cycle. |
| `unpopulated_table_index` | A reference in a `packed<T>` message names an entry that its table does not have. |
| `nodelist_too_long` | One step of a query selects more nodes than the item has bytes. |
| `duplicate_key` | A deterministic check finds a key twice in one map. |
| `no_buffer_space`, `value_too_large`, `not_enough_memory`, `io_error` | The writer of an encode gives this error. `not_enough_memory` also comes from a refused allocation in `structs` and `packed` decode. |
| `string_length_exceeded`, `container_elements_exceeded`, `input_bytes_exceeded` | A limit is in force and the input goes past it (section 4). |

```cpp
// [1, 2] without its last byte
cbor::lazy::from(bytes)->at(1).error();                  // too_little_data
// [_ 1]
cbor::lazy::from(bytes)->at(0).error();                  // indefinite_length
// {"a": 1}
cbor::lazy::from(bytes)->at("b").error();                // key_not_found
// 1
cbor::lazy::from(bytes)->at(0).error();                  // not_indexable
// "a"
cbor::lazy::from(bytes)->get<std::int64_t>().error();    // incorrect_type
// 256
cbor::lazy::from(bytes)->get<std::uint8_t>().error();    // number_out_of_range
```

## 4. Limits

`cbor::limits` holds the limits that are in force. Each call reads it once
at its start. An assignment changes the limits for every later call in
every thread. A call that runs keeps the limits that it read.

| Field | Default | Bound | Error |
|---|---|---|---|
| `nesting_depth` | 128 | 512 | `nesting_depth_exceeded` |
| `string_length` | `SIZE_MAX` | `SIZE_MAX` | `string_length_exceeded` |
| `container_elements` | `SIZE_MAX` | `SIZE_MAX` | `container_elements_exceeded` |
| `input_bytes` | `SIZE_MAX` | `SIZE_MAX` | `input_bytes_exceeded` |

A limit is checked by the step that reads the item. `lazy::from` checks
only `input_bytes`. So a string that is too long gives
`string_length_exceeded` at `get`, not at `lazy::from`.
`input_bytes` is the size of the bytes that a call over bytes gets.

A field reads as a `std::size_t`. An assignment to a field above its bound
throws `std::logic_error` and does not change the field. An assignment of
`cbor::limit_values` sets all four fields. A field that the designated
initializers do not name gets its default, not its old value.

```cpp
std::size_t const depth = cbor::limits.nesting_depth;   // 128
cbor::limits.string_length = 64;
cbor::limits = {.string_length = 2, .container_elements = 8};  // nesting_depth is 128 again
cbor::limits.nesting_depth = 513;                       // throws std::logic_error
```

Each default is a macro. A program defines the macro before it includes
the library, or with `-D` on the command line:

| Macro | Field |
|---|---|
| `CBOR_NESTING_DEPTH_DEFAULT` | `nesting_depth` |
| `CBOR_STRING_LENGTH_DEFAULT` | `string_length` |
| `CBOR_CONTAINER_ELEMENTS_DEFAULT` | `container_elements` |
| `CBOR_INPUT_BYTES_DEFAULT` | `input_bytes` |

```sh
g++ -DCBOR_NESTING_DEPTH_DEFAULT=32 ...
```

A default above the bound does not compile. All translation units of one
program use the same values: a different value is a violation of the one
definition rule.

The limits count what a call reads or decodes. An item that a path or a
lazy only skips does not count. `item_size`, `decode`, `lazy_decode` and
`diagnostic_notation` read every item, so every item counts there.

```cpp
cbor::limits = {.string_length = 2};
// {"a": "xyz", "n": 1}
cbor::lazy::from(bytes)->at("n")->get<std::int64_t>();        // 1: "xyz" is only skipped
cbor::lazy::from(bytes)->at("a")->get<std::string_view>();    // string_length_exceeded
cbor::item_size(bytes);                                       // string_length_exceeded
```

## 5. Nesting depth

The nesting depth counts the arrays, maps and tags around an item. The
top-level item has the depth 0. An item deeper than `nesting_depth` gives
`nesting_depth_exceeded` where a call recurses: `decode`, `lazy_decode`,
`encode`, `diagnostic_notation`, a path and a filter.

An item that a call only skips can be deeper than the limit, and so can
the item of `item_size`: neither keeps a stack.

```cpp
cbor::limits = {.nesting_depth = 2};
// [[1]]
cbor::diagnostic_notation(bytes);   // "[[1]]"
// [[[1]]]
cbor::diagnostic_notation(bytes);   // nesting_depth_exceeded
cbor::item_size(bytes);             // 4
```

## 6. Memory

The caller sets the bound of the memory of a decode. `structs<T>::decode`
and `packed<T>::decode` take a target as their third argument. The target
holds `std::pmr` containers with a `std::pmr::memory_resource`. The decode
reads into the target and makes each element with the allocator of its
container. When the resource refuses, the decode returns
`error::not_enough_memory`. A `std::bad_alloc` does not leave
`structs<T>::decode` or `packed<T>::decode`.

```cpp
using texts = std::pmr::vector<std::pmr::string>;
auto const owner = std::make_shared<std::string const>(
    *cbor::structs<std::vector<std::string>>::encode({std::string(40, 'a'), std::string(40, 'b')}));
std::array<std::byte, 64> buffer{};
std::pmr::monotonic_buffer_resource budget(buffer.data(), buffer.size(), std::pmr::null_memory_resource());
cbor::structs<texts>::decode(owner, *owner, texts(&budget));   // not_enough_memory
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
| a step of a lazy (`at`, `find`, `get`) that meets no tag 29 | nothing |
| `lazy::from` | the shared state of the top-level item, once; the forms with `std::string_view` and `std::string` also the `std::shared_ptr<std::string const>` that holds the bytes |
| the first tag 29 of a top-level item | the marks of the tags 28 before it |
| `lazy::decode` | its items |
| a binding | its own values; the library does not choose their memory |
| `diagnostic_notation`, `encode` into a `std::string` | the string |

The writer of `encode` chooses the memory of the bytes
([runtime.md](runtime.md)).

## 7. Threads

The library starts no thread and holds no lock. The only global is
`cbor::limits`, and each field is atomic.

A call over bytes, for example `at_path<Path, T>(encoded)` and
`item_size`, has no shared state. Two lazy values from two calls of
`lazy::from` share nothing, also over the same bytes.

The lazy values of one top-level item share their marks of tag 28, and a
read of a tag 29 adds marks with no lock. So a `cbor::lazy` belongs to one
thread. `cbor::is_thread_bound_v<T>` is true for `lazy`, `lazy_elements`,
`lazy_entries`, their iterators and every `owning_ref`.
`cbor::is_sendable_v<T>` is its opposite, and true for `cbor::sendable<T>`.
An application queue can check `is_sendable_v` at compile time.

```cpp
static_assert(cbor::is_thread_bound_v<cbor::lazy>);
static_assert(cbor::is_sendable_v<std::string>);
static_assert(cbor::is_sendable_v<cbor::sendable<cbor::lazy>>);
```

`cbor::transfer(std::move(l))` gives a `cbor::sendable<cbor::lazy>`. It
throws `std::logic_error` when another lazy, iterator or view shares the
top-level item of `l`. The thread that receives it reads `.value`.
`lazy::from` gives a `std::expected`, and `std::move(*l)` moves the lazy
out of it:

```cpp
// [1, 2]
auto l = cbor::lazy::from(bytes);
std::jthread([](cbor::sendable<cbor::lazy> const &v) {
    v.value.at(1)->get<std::int64_t>();   // 2
}, cbor::transfer(std::move(*l)));
```

A debug build is a build without the macro `NDEBUG`. A debug build records
the thread that reads the marks first. A read of the marks in another
thread then throws `std::logic_error`. A build with `NDEBUG` does not
check. Only an item with a tag 28 or a tag 29 has marks. A read that does
not touch the marks is not checked. So a lazy over `{"a": 1}` does not
throw in another thread, but it is still a wrong use.

## 8. Owners

A `cbor::lazy` holds the owner of the bytes. So does every lazy that a
step gives, and every iterator. A lazy from `at` stays valid after the
lazy that gave it and after the `std::string` that went into
`lazy::from` end.

```cpp
std::optional<cbor::lazy> keep;
{
    std::string bytes = message();   // {"tags": ["a", "b"]}
    keep = *cbor::lazy::from(std::move(bytes))->at("tags");
}
keep->at(1)->get<std::string_view>();   // "b"
```

- `*r` gives the `T const &`, and `r->` reaches its members.
- `*` and `->` do not compile on a temporary `owning_ref`.
- `==` compares the value with a `T` or with another `owning_ref`, as
  `std::optional` does. `==` takes a temporary, because it gives a `bool`.
- A copy of an `owning_ref` shares the owner. It does not copy the bytes.

The owner of the bytes comes from one of these:

| Call | Owner |
|---|---|
| `lazy::from(std::string)` with an rvalue | the library moves the string into a new `std::shared_ptr<std::string const>` |
| `lazy::from(std::string_view)` | the library copies the bytes into a new `std::shared_ptr<std::string const>` |
| `lazy::from(std::shared_ptr<std::string const>)` | the caller |
| `lazy::from(owner, encoded)` | the caller: `owner` holds the bytes of `encoded` |

`structs<T>::decode`, `packed<T>::decode` and `packed<T>::path` take the
same forms. `at_path` takes an owner for a view result
([path.md](path.md)).

The form with an owner reads bytes that something else holds, for example
a page of a read transaction of a database. The owner can be any
`std::shared_ptr`, and the aliasing constructor of `std::shared_ptr` makes
one from an object that holds the bytes.

```cpp
struct page {
    std::string bytes;   // "xyz"
};
auto const p = std::make_shared<page const>(message());
std::shared_ptr<void const> const owner(p, p->bytes.data());   // shares the count of p
cbor::lazy::from(owner, p->bytes)->get<std::string_view>();    // "xyz", a view into p->bytes
```

An empty owner throws `std::logic_error`. A temporary `std::string` or a
moved `std::string` beside an owner does not compile, because the owner
does not hold it. The library does not check that the owner holds the
bytes of `encoded`.
