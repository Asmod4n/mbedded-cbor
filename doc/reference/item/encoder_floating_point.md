# cbor::encoder::floating_point

```cpp
template <std::floating_point T>
    requires(std::numeric_limits<T>::is_iec559 &&
             (sizeof(T) == sizeof(std::uint32_t) || sizeof(T) == sizeof(std::uint64_t) ||
              (sizeof(T) == sizeof(std::uint16_t) &&
               std::numeric_limits<T>::digits == heads::half_precision.significand_bits + 1)))
std::expected<void, error> floating_point(T const value);
```

Writes `value` as a float in the width of `T`: 16, 32 or 64 bits. It
does not choose a shorter form. A NaN keeps its bits.

`T` is an IEEE 754 type of 16, 32 or 64 bits. Another float type does
not compile.

## Errors

`no_buffer_space` over a span that is too short. The error of a writer.
A call after an error or after `finish` throws `std::logic_error`.

## Cost

O(1). A container target can grow.

## Example

```cpp
std::string out;
cbor::encoder e(out);
e.floating_point(1.5f);
e.finish();   // out holds fa 3f c0 00 00
```

## See also

[simple_value](encoder_simple_value.md) · [encoder](encoder.md)
