# cbor::encode_iov

```cpp
std::expected<ref<std::span<iovec const>>, error> encode_iov(item const &root);
```

```call-seq
cbor::encode_iov(**tree) → std::expected<ref<std::span<iovec const>>, error>
```

```output
bytes: 82 01 61 61
call: auto const pieces = cbor::encode_iov(**cbor::decode(bytes))
(*pieces)->size(): 1
sent with writev: [1, "a"]
```

Writes the tree at `root` into a list of `struct iovec` pieces, not into
one buffer. The result gives the pieces in order and holds their storage
and the owners of the pieces that refer to strings. The pieces are for
sending only: `writev`, `sendmsg`, `IORING_OP_WRITEV` and
`IORING_OP_SENDMSG`. The program holds the `ref` until the call returns or
until the last completion of the request. The bytes are those of
[encode](encode.md). The function exists where `<sys/uio.h>` exists.

## Errors

`nesting_depth_exceeded`: the tree is deeper than the limit. A piece that
refers to the string of an owner points into memory that the owner holds
as const; a call that writes into the pieces, such as `readv`, is a wrong
use.

## Cost

O(nodes + bytes of the strings). The call copies each byte once, into
blocks of 16 KiB. One allocation for each block, and one for the
storage.

## Example

```cpp
// bytes: a std::string with [1, "a"]
auto const tree = cbor::decode(bytes);
auto const pieces = cbor::encode_iov(**tree);
(*pieces)->size();   // 1
writev(fd, (*pieces)->data(), static_cast<int>((*pieces)->size()));
// fd holds [1, "a"], the 4 bytes 82 01 61 61
```

## See also

[encode](encode.md) · [iov_writer](iov_writer.md)
