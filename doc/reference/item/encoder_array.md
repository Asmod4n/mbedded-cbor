# cbor::encoder::array

```cpp
std::expected<void, error> array(std::uint64_t const count);
```

```call-seq
e.array(count) → std::expected<void, error>
```

```example
bytes: 82 18 01 18 02
call: std::string out;
result: ""
call: cbor::encoder e(out);
result: encoder over a std::string
call: e.array(2)
result: void
call: e.unsigned_integer(std::uint8_t{1})
result: void
call: e.unsigned_integer(std::uint8_t{2})
result: void
call: e.finish()
result: 5
call: out
result: 5 bytes: 82 18 01 18 02
```

Writes the head of an array (major type 4) with `count` elements, with the shortest head for `count`. The
next calls write the elements. The encoder does not count them: a wrong
count gives bytes that are not one item.

## Errors

`no_buffer_space` over a span that is too short. The error of a writer.
A call after an error or after `finish` throws `std::logic_error`.

## Cost

O(1). A container target can grow.

## Example

```cpp
std::string out;
cbor::encoder e(out);
e.array(2);
e.unsigned_integer(std::uint8_t{1});
e.unsigned_integer(std::uint8_t{2});
e.finish();   // out holds 82 18 01 18 02
```

## See also

[map](encoder_map.md) · [encoder](encoder.md)
