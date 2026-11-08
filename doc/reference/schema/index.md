# schema

`cbor::schema<T>` writes a struct as Packed CBOR and reads each field of
fixed size at an offset that the compiler computes. It reads only the
description of each struct and builds on every compiler. With
reflection it also checks each description against the members. `path` opens a message in O(1) of its size, and a read of a field of fixed size is one jump that checks only the bytes it reads. `at_path` with no accessor allocates nothing. `encode` is O(bytes), and a list of fixed-size structs is one copy.

The examples use the types in [test/doc_types.hpp](../../../test/doc_types.hpp), namespace `doc_reference_schema`.

| Function | Does |
|---|---|
| [encode](encode.md) | writes a struct as one Packed CBOR message |
| [decode](decode.md) | reads a whole message into a struct |
| [path](path.md) | opens a message and gives the root accessor |
| [at_path](at_path.md) | reads one number, `bool` or enum with no accessor |
| [unverified_path](unverified_path.md) | opens a message without the checks of the data, for a peer that you trust |
| [unverified_at_path](unverified_at_path.md) | reads one value without the checks of the data |
| [unverified_decode](unverified_decode.md) | reads a whole message without the checks of the data |
| [description](description.md) | gives the description of a struct |
| [description_place](description_place.md) | writes and reads a message without the description |
| [fixed_size](fixed_size.md) | gives the count of bytes of a field of a type |
| [member_offset](member_offset.md) | gives the offset of a member in its record |
| [accessor](accessor.md) | reads a part of a message by a path |
| [fixed_element](fixed_element.md) | reads one element of a vector of fixed-size structs |
| [typed_array_view](typed_array_view.md) | a range over a list of numbers in the message |
| [tag](tag.md) | the tag number of a struct in its description |
| [wire](wire.md) | the bytes that `encode` writes |
