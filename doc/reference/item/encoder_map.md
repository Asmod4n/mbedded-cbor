# cbor::encoder::map

```cpp
cbor::result<void> map(std::uint64_t const count);
```

Writes the head of a map (major type 5) with `count` pairs, with the shortest head for `count`. The
next calls write each key and then its value. The encoder does not count them: a wrong
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
e.map(1);
e.text_string("a");
e.unsigned_integer(std::uint8_t{5});
e.finish();   // out holds a1 61 61 18 05
```

## See also

[array](encoder_array.md) · [encoder](encoder.md)
