# API reference

mbedded-cbor is a header-only C++ library for CBOR (RFC 8949). Its goal:
a read touches only the bytes that it needs, and copies none of them. A
read goes to one value and skips every other item by its length. A text
comes back as a view into the message. A struct that you describe is
written as Packed CBOR, so a read of a field is one jump to an offset
that the compiler knows. Every message that the library writes is
standard CBOR, and every other CBOR decoder reads it. Bad input comes
back as a `cbor::error` value, and no read goes outside the message.

One page per function and type. Each page gives the declaration, what
the call does, its errors, its cost, one example and links. The guides in
[doc/](..) show the common cases first.

| Group | Contents |
|---|---|
| [lazy](lazy/index.md) | `cbor::lazy`: read a message in place, step by step |
| [path](path/index.md) | `cbor::at_path`: read one value by its JSONPath |
| [item](item/index.md) | `cbor::item`, `decode`, `encode`: a decoded tree and the encoder |
| [structs](structs/index.md) | `cbor::structs<T>`: any CBOR into a C++ struct and back |
| [schema](schema/index.md) | `cbor::schema<T>`: structs as Packed CBOR, fields at fixed offsets |
| [ref](ref/index.md) | `cbor::ref<T>`, `cbor::owning<T>`: results that hold the bytes |
| [tools](tools/index.md) | `item_size`, `sequence`, `sequence_reader`, `diagnostic_notation` |
| [errors](errors/index.md) | `cbor::error`, `std::error_code`, the limits of a build |

## Which class to use

### lazy

Use it to walk a message whose shape you learn while you read it: a
map, then a key, then an element. Cost: a step by key or index reads the
heads up to the child, O(bytes before the child). No allocation. A text
is a view into the message.

### path

Use it to read one value when you know its path, for example
`$.user.id`. The path is checked at compile time. Cost: O(bytes up to
the value). No allocation for a number, a `bool` or a view. A path that
selects many nodes allocates its list.

### item

Use it when you need the whole message as a tree that lives on after the
bytes are gone, or to write CBOR item by item with the encoder. Cost:
`decode` is O(bytes) and copies every string into one memory zone. The
encoder is O(1) per head and allocates nothing over a `std::span<char>`.

### structs

Use it to read CBOR that any program wrote into the members of a struct,
and to write it back. Cost: O(bytes) for a read and a write. The result
allocates its strings and containers.

### schema

Use it for messages that your own programs write and read. Cost: `path`
opens a message in O(1) of its size, and a read of a field of fixed size
is one jump that checks only the bytes it reads. `at_path` with no accessor allocates nothing.
`encode` is O(bytes), and a list of fixed-size structs is one copy.

### ref

Use it to keep a result that points into the message valid: it holds the
owner of the bytes. Cost: O(1) for each access; a copy changes one
atomic count. `clone` copies, O(bytes).

### tools

Use `item_size` to find where an item ends, `sequence` and
`sequence_reader` to read many items one after the other, and
`diagnostic_notation` to print an item as text. Cost: `item_size` is
O(items) with no allocation. `sequence_reader` keeps only the bytes not
yet read. `diagnostic_notation` is O(bytes) and allocates its text.

### errors

Use it to read why a call failed: each failure is one `cbor::error`, and
it converts to `std::error_code`. Cost: none; an error is a small value.
