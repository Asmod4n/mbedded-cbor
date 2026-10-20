# cbor::encode

```cpp
template <class Target>
cbor::result<std::size_t> encode(item const &root, Target &&target);
```

Writes the tree at `root` and gives the count of bytes written. Each
integer, length and tag number keeps the width of its head, from
`additional_information`. A float keeps its width. A tag 28 and a tag 29
are written as they are. The target is one of these:

- a container of bytes, for example `std::string`: the call appends;
- a `std::span` of bytes: the call writes into it and allocates nothing;
- an object with `allocate(std::size_t)`, which gives a writer with
  `append(std::string_view)` and `done(std::size_t)`.

## Errors

- `no_buffer_space`: the span is too short.
- `nesting_depth_exceeded`: the tree is deeper than the limit.
- `value_too_large`, `not_enough_memory`, `io_error`: the error of a
  writer, from its `std::errc`.

## Cost

O(nodes + bytes of the strings). A container grows; a span takes no
allocation. The stack grows with the depth of the tree.

## Example

```cpp
// bytes: a std::string with [1, "a"]
auto const tree = cbor::decode(bytes);
std::string out;
cbor::encode(**tree, out);   // 4
out == bytes;                // true
```

## See also

[encode_iov](encode_iov.md) · [decode](decode.md) · [encoder](encoder.md)
