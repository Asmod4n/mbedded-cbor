# cbor::diagnostic_notation

```cpp
std::expected<std::string, error> diagnostic_notation(std::string_view encoded);
```

```call-seq
cbor::diagnostic_notation(bytes) → std::expected<std::string, error>
```

```example
bytes: a1 61 61 83 01 f9 41 00 41 01
call: cbor::diagnostic_notation(bytes)
result: "{\"a\": [1, 2.5, h'01']}"
```

```example
bytes: 9f 01 ff
call: cbor::diagnostic_notation(bytes)
result: "[_ 1]"
```

```example
bytes: c1 00
call: cbor::diagnostic_notation(bytes)
result: "1(0)"
```

Gives the text of the item in `encoded` in the diagnostic notation of
RFC 8949 section 8. A head that is not in preferred form carries its
encoding indicator, for example `1_0`. Indefinite length is printed, for
example `[_ 1]`. A tag is printed as its number and its content; tag 29
is not resolved.

`encoded` holds exactly one item. The call does not check tag content,
UTF-8 or duplicate keys.

## Errors

- `too_little_data`: the bytes end before the item ends.
- `syntax_error`: a head is not well-formed, a chunk of an indefinite
  string has the wrong type, or bytes follow the item.
- `nesting_depth_exceeded`: the item is deeper than
  [limits](../errors/limits.md)`::nesting_depth`.
- `string_length_exceeded`, `container_elements_exceeded`: a limit is set
  and the item goes past it.

## Cost

O(n). The string allocates; its size is O(n). The stack is O(d), d the
nesting depth.

## Example

```cpp
// bytes: {"a": [1, 2.5, h'01']}
cbor::diagnostic_notation(bytes);   // "{\"a\": [1, 2.5, h'01']}"
// bytes: 9f 01 ff
cbor::diagnostic_notation(bytes);   // "[_ 1]"
// bytes: 01 02
cbor::diagnostic_notation(bytes);   // syntax_error
```

## See also

[item_size](item_size.md) · [../../advanced/basics.md](../../advanced/basics.md), section 5
