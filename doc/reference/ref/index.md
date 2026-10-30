# ref

`cbor::ref<T>` and `cbor::owning<T>` hold a result together with the owner of the bytes that its views point into.

The examples use the types in [test/doc_types.hpp](../../../test/doc_types.hpp), namespace `reference_ref`.

| Function | Does |
|---|---|
| [ref&lt;T&gt;](ref.md) | holds a value and the owner of the bytes it views |
| [owning&lt;T&gt;](owning.md) | holds a copy of a value and of the bytes of its views |
