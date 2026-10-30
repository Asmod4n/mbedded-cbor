# std::is_error_code_enum<cbor::error>

```cpp
template <>
struct std::is_error_code_enum<cbor::error> : std::true_type {};
```

```call-seq
std::is_error_code_enum_v<cbor::error>  → bool
std::error_code e{cbor::error::x}       → std::error_code
```

```example
message: {}
call: std::is_error_code_enum_v<cbor::error>
result: true
call: std::error_code const e{cbor::error::index_out_of_bounds}
result: std::error_code cbor:12
call: e.message()
result: "index outside of array bounds"
```

Marks `cbor::error` as an error code enum. So a `cbor::error` converts to
a `std::error_code` with no cast, through
[make_error_code](make_error_code.md). A `std::error_code` also compares
equal to a `cbor::error` with `==`.

A `cbor::error` and a `std::errc` do not compare directly: that does not
compile. Put a `std::error_code` on the left.

## Errors

None.

## Cost

None at run time.

## Example

```cpp
std::error_code const e = cbor::error::index_out_of_bounds;
e.message();                               // "index outside of array bounds"
e == cbor::error::index_out_of_bounds;     // true
```

## See also

[make_error_code](make_error_code.md) · [error_category](error_category.md) · [../../advanced/basics.md](../../advanced/basics.md), section 1
