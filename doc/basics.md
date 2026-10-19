# Basics

Every part of the library follows four rules: how an error comes back,
how long a result stays valid, how threads read, and how a limit is set.
This page states them.

| Task | Call | Document |
|---|---|---|
| Read one value | `cbor::at_path` | [path.md](path.md) |
| Walk through arrays and maps | `cbor::lazy` | [runtime.md](runtime.md) |
| Write a struct and read it back | `cbor::structs` | [structs.md](structs.md) |
| Read one field of a large message | `cbor::schema` | [schema.md](schema.md) |

The library needs C++23. `structs` and `schema` need g++ 16 with
`-std=c++26 -freflection`. Every example includes `<cbor/cbor.hpp>`.
A comment shows the CBOR in `bytes` as text, in the diagnostic notation
of RFC 8949, which looks like JSON.

## 1. Errors

A call that reads CBOR returns a `std::expected<T, cbor::error>`. It
holds the value or the error. Bad input never throws.

```cpp
// bytes: {"user": {"id": 42}}
auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
if (id)
    use(*id);                                           // 42
else
    report(std::error_code{id.error()}.message());

auto const age = cbor::at_path<"$.user.age", std::int64_t>(bytes);
age.error() == cbor::error::key_not_found;              // true
std::error_code{age.error()}.message();                 // "key not found"
```

These are the errors that a program meets most often:

| Error | When | Example |
|---|---|---|
| `key_not_found` | The map has no such key. | `$.b` in `{"a": 1}` |
| `index_out_of_bounds` | The array has no element at the index. | `$[2]` in `[1, 2]` |
| `not_indexable` | A key or an index meets a value that is not a map or an array. | `$.a.b` in `{"a": 1}` |
| `incorrect_type` | The value has another type than the type you read. | `"a"` read as `int` |
| `number_out_of_range` | The number does not fit the type you read. | `256` read as `std::uint8_t` |
| `too_little_data` | The input ends before the value ends. | `$[1]` in `[1, 2]` without its last byte |
| `syntax_error` | The bytes are not CBOR. | the byte `ff` |
| `indefinite_length` | The input uses indefinite length. The library does not read it. | `$[0]` in `[_ 1]` |
| `invalid_path` | A path given at run time is not valid. | the path `$.a[` |
| `nesting_depth_exceeded` and the other `..._exceeded` errors | The input goes past a limit (section 4). | `[1, 2, 3]` with `container_elements = 2` |

A wrong use of the library does not compile, or it throws
`std::logic_error`. Do not catch `std::logic_error`: it is a fault of the
program, not of the input.

## 2. Results that point into the bytes

A text or a byte string comes back as a view into the bytes, with no
copy. The view sits in a `cbor::owning_ref`. The `owning_ref` holds the
bytes, so the view stays valid while the `owning_ref` lives.

Bytes go into the library in two forms only. A `std::string_view` or a
`std::string` is copied into storage that the library owns; an rvalue
`std::string` is moved there with no copy. A
`std::shared_ptr<std::string const>` is read in place, and every result
keeps it alive. The library takes no other pointer to bytes.

```cpp
// bytes: {"name": "ann"}
auto const owner = std::make_shared<std::string const>(std::move(bytes));
auto const name = cbor::at_path<"$.name", std::string_view>(owner);
if (name) {
    std::string_view const text = **name;   // "ann"
    std::string const copy{text};           // a copy of your own
    *name == "ann";                         // true
}
```

`*name` is the `owning_ref`, and `**name` is the `std::string_view`.
`text` stays valid after `owner` ends, because `name` holds the bytes.
Keep a result in a variable: `*` on a temporary `owning_ref` does not
compile. A `cbor::lazy`, a result of `structs` and an accessor of
`schema` hold the bytes in the same way.

## 3. Threads

Many threads can read the same bytes at the same time, while nothing
writes them. A `cbor::lazy` and every result from it belong to the thread
that made them, so each thread makes its own lazy.

```cpp
// bytes: {"id": 42}
std::jthread first([&bytes] { use(cbor::at_path<"$.id", std::int64_t>(bytes)); });
std::jthread second([&bytes] {
    if (auto const doc = cbor::lazy::copy(bytes))   // the lazy of this thread
        use(doc->at_path<"$.id", std::int64_t>());
});
```

The library starts no thread and holds no lock.

## 4. Limits

A limit stops input that is too deep or that holds a string or a
container that is too large. A limit is a constant of the build: a macro
sets it, and `cbor::limits` gives its value.

| Macro | Constant | Counts | Default | Error |
|---|---|---|---|---|
| `CBOR_NESTING_DEPTH_DEFAULT` | `cbor::limits::nesting_depth` | the arrays, maps and tags around a value | 128, at most 512 | `nesting_depth_exceeded` |
| `CBOR_STRING_LENGTH_DEFAULT` | `cbor::limits::string_length` | the bytes of one text or byte string | no limit | `string_length_exceeded` |
| `CBOR_CONTAINER_ELEMENTS_DEFAULT` | `cbor::limits::container_elements` | the elements of one array, or the pairs of one map | no limit | `container_elements_exceeded` |

```sh
g++ -DCBOR_NESTING_DEPTH_DEFAULT=16 -DCBOR_STRING_LENGTH_DEFAULT=1024 ...
```

Every translation unit of one program uses the same values. A limit that
the build does not set gets its default. A check whose limit is not set
is not in the program. The library does not bound the size of the input:
the program bounds it before it calls the library.

The error comes at the read that meets the value. A count in the input
reserves no memory: memory grows only with the values that a call reads.

More: every error value, the limits in detail, memory, and a lazy that
moves to another thread are in [advanced/basics.md](advanced/basics.md).
