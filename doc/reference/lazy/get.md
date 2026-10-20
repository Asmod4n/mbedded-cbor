# cbor::lazy::get

```cpp
template <class T>
    requires(std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
             std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value> ||
             std::is_same_v<T, std::string_view> ||
             std::is_same_v<T, std::span<std::byte const>> || std::is_same_v<T, typed_array>
#ifdef __STDCPP_FLOAT128_T__
             || std::is_same_v<T, std::float128_t>
#endif
cbor::result<std::conditional_t<std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
                                   std::is_same_v<T, typed_array>,
                               ref<T>, T>> get() const;
```

Reads the value at the lazy as `T`.

| T | Reads |
|---|---|
| an integer type | an integer or a bignum that fits `T` |
| `double` | a float, or an integer that a `double` holds exactly |
| `bool`, `std::nullptr_t` | `false` or `true`, `null` |
| `cbor::simple_value` | a simple value |
| `std::string_view`, `std::span<std::byte const>` | a text, a byte string, as a `ref` |
| `cbor::typed_array` | a typed array (RFC 8746), as a `ref` |
| `std::float128_t` | an element of a typed array of 128-bit floats |

A `ref` views the bytes and holds their owner. Nothing is copied. The
call passes a tag that it does not interpret.

## Errors

- `incorrect_type`: the value is of another kind.
- `number_out_of_range`: the number does not fit `T`.
- `too_little_data`: a string is longer than the bytes that are left.

## Cost

O(1). O(bytes of the magnitude) for a bignum. No allocation.

## Example

```cpp
// bytes: a std::string with {"name": "ann", "age": 300}
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
auto const name = doc->at("name")->get<std::string_view>();
**name;                                       // "ann"
doc->at("age")->get<std::int64_t>();          // 300
doc->at("age")->get<std::uint8_t>().error();  // number_out_of_range
doc->at("name")->get<int>().error();          // incorrect_type
```

## See also

[at](at.md) · [type](type.md) · [clone](clone.md) · [../../runtime.md](../../runtime.md), section 2
