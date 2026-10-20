# cbor::encoder::simple_value

```cpp
cbor::result<void> simple_value(cbor::simple_value const value);
```

Writes a simple value (major type 7), for example `false_value`,
`true_value`, `null` or `undefined`. A value from 0 to 23 takes one
byte, and a value from 32 to 255 takes two.

## Errors

- `reserved_simple_value`: `value` is from 24 to 31 (RFC 8949 3.3).
- `no_buffer_space` over a span that is too short. The error of a
  writer.
- A call after an error or after `finish` throws `std::logic_error`.

## Cost

O(1). A container target can grow.

## Example

```cpp
std::string out;
cbor::encoder e(out);
e.simple_value(cbor::simple_value::null);
e.finish();   // out holds f6
```

## See also

[floating_point](encoder_floating_point.md) · [encoder](encoder.md)
