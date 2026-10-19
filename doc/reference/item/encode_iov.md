# cbor::encode_iov

```cpp
std::expected<ref<std::span<std::string_view const>>, error> encode_iov(item const &root);
```

Writes the tree at `root` into a list of pieces, not into one buffer.
The result gives the pieces in order and holds their storage. A
program gives the pieces to `writev` or to a socket, or joins them. The
bytes are those of [encode](encode.md).

## Errors

`nesting_depth_exceeded`: the tree is deeper than the limit.

## Cost

O(nodes + bytes of the strings). The call copies each byte once, into
blocks of 16 KiB. One allocation for each block, and one for the
storage.

## Example

```cpp
// bytes: a std::string with [1, "a"]
auto const tree = cbor::decode(bytes);
auto const pieces = cbor::encode_iov(**tree);
std::string joined;
for (std::string_view const piece : **pieces)
    joined += piece;
joined == bytes;   // true
```

## See also

[encode](encode.md) · [iov_writer](iov_writer.md)
