# cbor::encoder::typed_array

```cpp
std::expected<void, error> typed_array(cbor::typed_array const value);
```

```call-seq
e.typed_array(value) → std::expected<void, error>
```

```example
bytes: d8 40 42 01 02
call: std::array<std::uint8_t, 2> two{1, 2};
result: {1, 2}
call: std::string out;
result: ""
call: cbor::encoder e(out);
result: encoder over a std::string
call: e.typed_array(cbor::typed_array{64, std::as_bytes(std::span(two))})
result: void
call: e.finish()
result: 5
call: out
result: 5 bytes: d8 40 42 01 02
call: std::string out2;
result: ""
call: cbor::encoder e2(out2);
result: encoder over a std::string
call: e2.typed_array(cbor::typed_array{66, std::as_bytes(std::span(two))})
result: unexpected cbor::error::inadmissible_type_for_tag_content
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
