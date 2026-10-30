# cbor::encoder::tag

```cpp
std::expected<void, error> tag(std::uint64_t const number);
```

```call-seq
e.tag(number) → std::expected<void, error>
```

```example
bytes: d9 d9 f7 18 01
call: std::string out;
result: ""
call: cbor::encoder e(out);
result: encoder over a std::string
call: e.tag(55799)
result: void
call: e.unsigned_integer(std::uint8_t{1})
result: void
call: e.finish()
result: 5
call: out
result: 5 bytes: d9 d9 f7 18 01
```

Writes the head of a tag (major type 6) with the tag number `number`,
with the shortest head. The next call writes the content. The call does
not check that the content fits the tag.

## Errors

`no_buffer_space` over a span that is too short. The error of a writer.
A call after an error or after `finish` throws `std::logic_error`.

## Cost

O(1). A container target can grow.

## Example

```cpp
std::string out;
cbor::encoder e(out);
e.tag(55799);
e.unsigned_integer(std::uint8_t{1});
e.finish();   // out holds d9 d9 f7 18 01
```

## See also

[typed_array](encoder_typed_array.md) · [encoder](encoder.md)
