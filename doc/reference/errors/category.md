# cbor::category

```cpp
std::error_category const &category() noexcept;
```

```call-seq
cbor::category()        → std::error_category const &
cbor::category().name() → char const *
```

```example
message: {}
call: std::error_code const e{cbor::error::syntax_error}
result: std::error_code cbor:2
call: e.category().name()
result: "cbor"
call: cbor::category()
result: std::error_category "cbor"
```

Gives the one [error_category](error_category.md) of `cbor::error`. Each
call gives the same object. Its name is `"cbor"`. A `std::error_code`
made from a `cbor::error` holds this category.

## Errors

None.

## Cost

O(1). The object is a function-local static: the first call constructs
it. No allocation.

## Example

```cpp
std::error_code const e = cbor::error::syntax_error;
e.category() == cbor::category();   // true
cbor::category().name();            // "cbor"
```

## See also

[error_category](error_category.md) · [make_error_code](make_error_code.md) · [error](error.md)
