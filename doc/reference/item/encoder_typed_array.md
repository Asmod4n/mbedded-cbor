# cbor::encoder::typed_array

```cpp
cbor::result<void> typed_array(cbor::typed_array const value);
```

Writes a typed array (RFC 8746): the tag `value.tag` and then
`value.bytes` as a byte string. The bytes are copied as they are, in
the byte order that the tag names.

## Errors

- `unsupported_value`: `value.tag` is not the tag of a typed array.
- `inadmissible_type_for_tag_content`: the count of bytes is not a
  multiple of the size of one element.
- `no_buffer_space` over a span that is too short. The error of a
  writer.
- A call after an error or after `finish` throws `std::logic_error`.

## Cost

O(bytes). A container target can grow.

## Example

```cpp
// two: a std::array<std::uint8_t, 2> with {1, 2}
std::string out;
cbor::encoder e(out);
e.typed_array(cbor::typed_array{64, std::as_bytes(std::span(two))});
e.finish();   // out holds d8 40 42 01 02, the tag 64 for uint8
```

## See also

[tag](encoder_tag.md) · [byte_string](encoder_byte_string.md) · [encoder](encoder.md)
