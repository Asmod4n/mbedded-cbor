# schema

`cbor::schema<T>` writes a struct as Packed CBOR and reads each field of
fixed size at an offset that the compiler computes. It reads only the
description of each struct and builds on every compiler. With
reflection it also checks each description against the members. A read of a field of fixed size checks only the bytes it reads.

The examples use the types in [test/doc_types.hpp](../../../../test/doc_types.hpp), namespace `doc_reference_schema`.

| Function | Does |
|---|---|
| [wire](wire.md) | The bytes that `encode` writes. |
