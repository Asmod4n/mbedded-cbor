# cbor::make_error_code

```cpp
std::error_code make_error_code(error const e) noexcept;
```

Makes a `std::error_code` with the value of `e` and the
[category](category.md) of `cbor::error`. The constructor of
`std::error_code` from a `cbor::error` calls it by argument-dependent
lookup. So a program seldom calls it by name.

## Errors

None.

## Cost

O(1). No allocation.

## Example

```cpp
std::error_code const e = cbor::make_error_code(cbor::error::invalid_path);
e.message();                                   // "invalid path"
e == std::error_code{cbor::error::invalid_path};   // true
```

## See also

[is_error_code_enum](is_error_code_enum.md) · [category](category.md) · [error](error.md)
