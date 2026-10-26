# API reference

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
