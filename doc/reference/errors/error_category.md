# cbor::error_category

```cpp
class error_category : public std::error_category
{
public:
    char const *name() const noexcept override;
    std::string message(int const condition) const override;
    std::error_condition default_error_condition(int const condition) const noexcept override;
};
```

```call-seq
cbor::category().name()                            → char const *
cbor::category().message(condition)                → std::string
cbor::category().default_error_condition(condition) → std::error_condition
```

```example
message: {}
call: std::error_code const e{cbor::error::no_buffer_space}
result: std::error_code cbor:21
call: e.default_error_condition().category().name()
result: "generic"
call: std::error_code const t{cbor::error::too_little_data}
result: std::error_code cbor:1
call: t.default_error_condition().category().name()
result: "cbor"
call: cbor::category().message(13)
result: "key not found"
call: cbor::category().message(0)
result: "unknown cbor error"
```

The `std::error_category` of `cbor::error`. A program reaches it through
[category](category.md). `name()` gives `"cbor"`. `message` gives the
text of the error, for example `"key not found"`, and
`"unknown cbor error"` for a value that is no `cbor::error`.

`default_error_condition` maps four errors to a `std::errc`:
`no_buffer_space`, `value_too_large`, `not_enough_memory` and `io_error`,
each to the `std::errc` of the same name. Every other error maps to a
condition of this category. So only these four compare equal to a
`std::errc`.

## Errors

None.

## Cost

`name` and `default_error_condition` are O(1). `message` allocates the
`std::string`.

## Example

```cpp
std::error_code{cbor::error::no_buffer_space} == std::errc::no_buffer_space;   // true
std::error_code{cbor::error::too_little_data} != std::errc::no_message;        // true
cbor::category().message(0);                                                   // "unknown cbor error"
```

## See also

[category](category.md) · [is_error_code_enum](is_error_code_enum.md) · [../../advanced/basics.md](../../advanced/basics.md), section 1
