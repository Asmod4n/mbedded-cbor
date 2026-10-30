# cbor::encoder::unsigned_integer

```cpp
template <class T>
    requires integer_argument<T>
std::expected<void, error> unsigned_integer(T const value);
```

```call-seq
e.unsigned_integer(value) → std::expected<void, error>
```

```example
bytes: 19 00 05
call: std::string out;
result: ""
call: cbor::encoder e(out);
result: encoder over a std::string
call: e.unsigned_integer(std::uint16_t{5})
result: void
call: e.finish()
result: 3
call: out
result: 3 bytes: 19 00 05
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
