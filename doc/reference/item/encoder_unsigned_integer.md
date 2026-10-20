# cbor::encoder::unsigned_integer

```cpp
template <class T>
    requires integer_argument<T>
cbor::result<void> unsigned_integer(T const value);
```

Writes `value` as an unsigned integer (major type 0). The head takes the
width of `T`: one byte of argument for `std::uint8_t`, eight for
`std::uint64_t`, also when a shorter head holds the value.

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
e.unsigned_integer(std::uint16_t{5});
e.finish();   // out holds 19 00 05
```

## See also

[negative_integer](encoder_negative_integer.md) · [encoder](encoder.md)
