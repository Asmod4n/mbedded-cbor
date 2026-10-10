# Basics

This document states what every API of the library shares: errors,
limits, memory, nesting depth, threads and ownership. `doc/runtime.md`
states the run-time API, and `doc/path.md` states the paths.

Each claim in this document has a test in `test/doc_basics.cpp`. The test
names start with `doc basics:`.

## Contents

1. Errors
2. Wrong use
3. The error values
4. Limits
5. Nesting depth
6. Memory
7. Threads
8. Ownership

## 1. Errors

A function that reads input gives `std::expected<T, cbor::error>`. Bad
input is a value, not an exception. The library throws no exception for
bad input.

`cbor::error` is an error code enum: `std::is_error_code_enum` is true for
it. So a `cbor::error` converts to a `std::error_code`. The category has
the name `cbor`. `message()` gives the text of the error.

```cpp
std::error_code const e = cbor::error::key_not_found;
e.message();          // "key not found"
e.category().name();  // "cbor"
```

Four errors compare equal to a `std::errc`:

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

Do not catch `std::logic_error` to repair input. It is a fault of the
program that calls the library.

## 3. The error values

| Error | When |
|---|---|
| `too_little_data` | The bytes end before the item ends. |
| `syntax_error` | A head is not well-formed, or a simple value is reserved in the two-byte form. |
| `indefinite_length` | A head uses indefinite length. The library does not read indefinite length. |
| `nesting_depth_exceeded` | The item is deeper than the nesting depth (section 5). |
| `inadmissible_type_for_tag_content` | A tag has content that its definition does not allow. |
| `sharedref_index_not_marked` | A tag 29 names no tag 28 before it. |
| `sharedref_index_out_of_range` | The index of a tag 29 does not fit `std::size_t`. |
| `sharedref_not_complete` | A tag 29 names a tag 28 that contains it, and the binding does not take a cycle. |
| `reserved_simple_value` | An encode meets a simple value from 24 to 31. |
| `unsupported_value` | An encode meets a value that the binding gives as `kind::unsupported`, or a typed array with a reserved tag. |
| `not_indexable` | A key or an index meets an item that is not a map or an array. |
| `index_out_of_bounds` | The index is outside the array. |
| `key_not_found` | The key is not in the map. |
| `invalid_path` | The path text is not valid (doc/path.md). |
| `incorrect_type` | A typed read finds another kind of item than T takes. |
| `number_out_of_range` | The number does not fit T. |
| `cyclic_data_structure` | An encode without sharing meets a cycle. |
| `unpopulated_table_index` | A reference in a `schema<T>` message names an entry that its table does not have. |
| `nodelist_too_long` | One step of a query selects more nodes than the item has bytes. |
| `duplicate_key` | A deterministic check finds a key twice in one map. |
| `no_buffer_space`, `value_too_large`, `not_enough_memory`, `io_error` | The writer of an encode gives this error. |
| `string_length_exceeded`, `container_elements_exceeded`, `input_bytes_exceeded` | A limit is in force and the input goes past it (section 4). |

```cpp
cbor::lazy::from("\x82\x01"s)->at(1)->get<int>().error();  // too_little_data
cbor::lazy::from("\x9f\x01\xff"s)->at(0).error();         // indefinite_length
cbor::lazy::from("\xa1\x61""a\x01"s)->at("b").error();    // key_not_found
cbor::lazy::from("\x01"s)->at(0).error();                 // not_indexable
cbor::lazy::from("\x61""a"s)->get<std::int64_t>().error();  // incorrect_type
cbor::lazy::from("\x19\x01\x00"s)->get<std::uint8_t>().error(); // number_out_of_range
```

## 4. Limits

`cbor::limits` holds the limits that are in force. It is one object for
the whole process. Each call reads it once at its start. An assignment
changes the limits for every later call in every thread. A call that runs
keeps the limits that it read.

| Field | Default | Bound | Error |
|---|---|---|---|
| `nesting_depth` | 128 | 512 | `nesting_depth_exceeded` |
| `string_length` | `SIZE_MAX` | `SIZE_MAX` | `string_length_exceeded` |
| `container_elements` | `SIZE_MAX` | `SIZE_MAX` | `container_elements_exceeded` |
| `input_bytes` | `SIZE_MAX` | `SIZE_MAX` | `input_bytes_exceeded` |

`string_length` is the length of one byte string or text string.
`container_elements` is the count of elements of an array, or the count
of pairs of a map. `input_bytes` is the size of the bytes that a call over
bytes gets.

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
lazy only skips does not count: the skip reads heads only and stops at the
end of the bytes. `item_size`, `decode`, `lazy_decode` and
`diagnostic_notation` read every head, so every head counts there.

A count in the input reserves no memory. Memory grows with the items that
the call read.

## 5. Nesting depth

The nesting depth counts the arrays, maps and tags around an item. The
top-level item has the depth 0. An item deeper than `nesting_depth` gives
`nesting_depth_exceeded` where a call recurses: `decode`, `lazy_decode`,
`encode`, `diagnostic_notation`, a path and a filter.

A skip keeps a count and no stack. So an item that a call only skips, and
`item_size`, can be deeper than the limit.

```cpp
cbor::limits = {.nesting_depth = 2};
cbor::lazy_decode(binding, *cbor::lazy::from("\x81\x81\x01"s));      // [[1]]
cbor::lazy_decode(binding, *cbor::lazy::from("\x81\x81\x81\x01"s));  // nesting_depth_exceeded
cbor::item_size("\x81\x81\x81\x01"sv);                               // 4
```

## 6. Memory

No function takes an allocator or a `std::pmr::memory_resource`. The
library allocates with `operator new`, through the standard containers and
`std::make_shared`. A failed allocation throws `std::bad_alloc`.

These calls allocate nothing:

- `at_path<Path, T>(encoded)` for a scalar T, `item_size`, the iterator
  of `cbor::sequence`;
- a step of a lazy (`at`, `find`, `get`) that meets no tag 29.

These calls allocate:

- `lazy::from` allocates the shared state of the top-level item once. The
  forms with `std::string_view` and with `std::string` also allocate the
  `std::shared_ptr<std::string const>` that holds the bytes;
- the first tag 29 of a top-level item allocates the marks of the tags 28
  before it;
- `lazy::decode` allocates its items;
- a binding allocates its own values. The library calls the binding and
  does not choose its memory;
- `diagnostic_notation` and `encode` into a `std::string` allocate the
  string.

A binding that uses `std::pmr` gives its own values a memory resource.
The writer of `encode` chooses the memory of the bytes (doc/runtime.md).

## 7. Threads

The library starts no thread and holds no lock. The only global is
`cbor::limits`, and each field is atomic.

Bytes that nothing writes can be read from many threads at the same time.
A call over bytes, for example `at_path<Path, T>(encoded)` and
`item_size`, has no shared state.

A `cbor::lazy` belongs to one thread. The lazy values of one top-level
item share their marks of tag 28, and a read of a tag 29 adds marks with
no lock. `cbor::is_thread_bound_v<T>` is true for `lazy`, `lazy_elements`,
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

```cpp
std::jthread([](cbor::sendable<cbor::lazy> const &v) {
    v.value.at(1)->get<std::int64_t>();
}, cbor::transfer(std::move(l)));
```

A debug build records the thread that reads the marks first. A read of the
marks in another thread then throws `std::logic_error`. A release build
does not check. A read that does not touch the marks is not checked.

Two lazy values from two calls of `lazy::from` share nothing, also over
the same bytes. So each thread can make its own lazy.

## 8. Ownership

A view result points into the bytes. It is a `cbor::owning_ref<T>`. It
holds a `std::shared_ptr<void const>` to the owner of the bytes, so the
bytes live as long as the view.

- `*r` gives the `T const &`, `r->` reaches its members.
- `*` and `->` do not compile on a temporary `owning_ref`. So a view
  cannot outlive its owner in one expression.
- `==` compares the value with a `T` or with another `owning_ref`, as
  `std::optional` does. `==` takes a temporary, because it gives a `bool`.
- A copy of an `owning_ref` shares the owner. It does not copy the bytes.

```cpp
auto const name = cbor::lazy::from("\xa1\x61""a\x63xyz"s)->at("a")->get<std::string_view>();
**name;            // "xyz"
(*name)->size();   // 3
*name == "xyz";    // true
```

The owner of the bytes comes from one of these:

| Call | Owner |
|---|---|
| `lazy::from(std::string)` with an rvalue | the library moves the string into a new `std::shared_ptr<std::string const>` |
| `lazy::from(std::string_view)` | the library copies the bytes into a new `std::shared_ptr<std::string const>` |
| `lazy::from(std::shared_ptr<std::string const>)` | the caller |
| `lazy::from(owner, encoded)` | the caller: `owner` holds the bytes of `encoded` |

The form with an owner reads bytes that something else holds, for example
a page of a read transaction of a database. The owner can be any
`std::shared_ptr`, and the aliasing constructor of `std::shared_ptr` makes
one from an object that holds the bytes.

An empty owner throws `std::logic_error`. A temporary `std::string` or a
moved `std::string` beside an owner does not compile, because the owner
does not hold it. The library does not check that the owner holds the
bytes of `encoded`.
