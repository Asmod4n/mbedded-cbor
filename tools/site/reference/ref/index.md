# ref

`cbor::ref<T>` and `cbor::owning<T>` hold a result together with the owner of the bytes that its views point into. A copy shares the owner. `clone` copies the bytes.

The examples use the types in [test/doc_types.hpp](../../../../test/doc_types.hpp), namespace `reference_ref`.

| Function | Does |
|---|---|
