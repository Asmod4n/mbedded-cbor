# schema

`cbor::packed<T>` writes a struct as Packed CBOR and reads each field of
fixed size at an offset that the compiler computes. It reads only the
description of each struct and builds on every compiler.
`cbor::schema<T>` has the same functions and writes the same bytes; it
exists only with reflection and checks each description against it.

| Function | Does |
|---|---|
| [encode](encode.md) | writes a struct as one Packed CBOR message |
| [decode](decode.md) | reads a whole message into a struct |
| [path](path.md) | opens a message and gives the root accessor |
| [at_path](at_path.md) | reads one number, `bool` or enum with no accessor |
| [unverified_path, unverified_at_path, unverified_decode](unverified.md) | the same reads without the checks of the data, for a peer that you trust |
| [description, description_place](description.md) | gives the description of a struct, and writes and reads a message without it |
| [fixed_size](fixed_size.md) | gives the count of bytes of a field of a type |
| [member_offset](member_offset.md) | gives the offset of a member in its record |
| [accessor](accessor.md) | reads a part of a message by a path |
| [fixed_element](fixed_element.md) | reads one element of a vector of fixed-size structs |
| [typed_array_view](typed_array_view.md) | a range over a list of numbers in the message |
| [tag](tag.md) | the tag number of a struct in its description |
| [wire](wire.md) | the bytes that `encode` writes |
