# cbor::item_size

```cpp
std::expected<std::size_t, error> item_size(std::string_view encoded);
```

```call-seq
cbor::item_size(bytes) → std::expected<std::size_t, error>
```

```example
bytes: 82 01 02 07
call: cbor::item_size(bytes)
result: 3
call: cbor::item_size(bytes.substr(0, 2))
result: unexpected cbor::error::too_little_data
```

```example
bytes: 9f 01 ff
call: cbor::item_size(bytes)
result: unexpected cbor::error::indefinite_length
```

Reads the first item in `encoded` to its end and gives its size in bytes.
It checks that the item is well-formed: each head, each string and each
element. Bytes after the item are not read and are not an error.

The call keeps no stack, so it checks no nesting depth: an item deeper
than `limits::nesting_depth` gives its size. It does not check tag
content, UTF-8 or duplicate keys.

## Errors

- `too_little_data`: the bytes end before the item ends, or a container
  claims more items than the bytes left.
- `syntax_error`: a head is not well-formed, for example `1c` or the
  reserved simple value `f8 1f`.
- `indefinite_length`: a head uses indefinite length.
- `string_length_exceeded`, `container_elements_exceeded`: a limit is set
  and a string or a container goes past it.

## Cost

O(i), i the items, bounded by O(n). Memory O(1). No allocation.

## Example

```cpp
// bytes: [1, 2] followed by 7, 4 bytes
cbor::item_size(bytes);                    // 3
cbor::item_size(bytes.substr(0, 2));       // too_little_data
// bytes: [_ 1]
cbor::item_size(bytes);                    // indefinite_length
```

## See also

[sequence](sequence.md) · [../errors/limits.md](../errors/limits.md) · [../../advanced/basics.md](../../advanced/basics.md), section 5
