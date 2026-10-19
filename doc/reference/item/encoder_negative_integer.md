# cbor::encoder::negative_integer

```cpp
template <class T>
    requires integer_argument<T>
std::expected<void, error> negative_integer(T const argument);
```

Writes a negative integer (major type 1) with the argument `argument`.
The value is -1 - `argument`. The head takes the width of `T`.

`T` is an unsigned integer type. `bool` and the character types do not
compile.

## Errors

`no_buffer_space` over a span that is too short. The error of a writer.
A call after an error or after `finish` throws `std::logic_error`.

## Cost

O(1). A container target can grow.

## Example

```cpp
std::string out;
cbor::encoder e(out);
e.negative_integer(std::uint16_t{499});
e.finish();   // out holds 39 01 f3, the value -500
```

## See also

[unsigned_integer](encoder_unsigned_integer.md) · [encoder](encoder.md)
