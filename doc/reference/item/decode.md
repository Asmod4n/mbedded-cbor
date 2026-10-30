# cbor::decode

```cpp
std::expected<owning<item>, error> decode(std::string_view encoded);
```

```call-seq
cbor::decode(bytes) → std::expected<owning<item>, error>
```

```example
message: ["hi", 1]
call: auto const tree = cbor::decode(bytes)
result: owning item of ["hi", 1]
call: (*tree)->get<cbor::type::array>()->size()
result: 2
```

```example
bytes: 01 02
call: cbor::decode(bytes)
result: owning item of 1
```

```example
bytes: c1 00
call: cbor::decode(bytes)
result: owning item of 1(0)
```

Decodes the one top-level item in `encoded` into a tree of
[item](item.md). It checks every head. The tree holds a copy of each
text and byte string, so it stays valid after `encoded` ends. A bignum
whose magnitude fits 64 bits becomes an integer node. A tag 24 is a tag
node, and [encoded_cbor_data_item](encoded_cbor_data_item.md) gives its
decoded content. Tags 28 and 29 stay tag nodes. A leading tag 55799
stays a tag node.

## Errors

- `too_little_data`: the item is not complete, or a count is larger
  than the bytes that are left.
- `syntax_error`: a head is not well-formed. A byte after the item is
  not read, so the input can be a CBOR sequence (RFC 8742).
- `indefinite_length`: a string, an array or a map has an indefinite
  length.
- `nesting_depth_exceeded`: the item is deeper than the limit.
- `reserved_simple_value`, `inadmissible_type_for_tag_content`: a value
  that RFC 8949 or RFC 8746 does not allow.

## Cost

O(bytes of the item). The tree takes one memory zone, which grows in
chunks, and one `std::shared_ptr`. A count in the input reserves
nothing.

## Example

```cpp
// bytes: a std::string with ["hi", 1]
auto const tree = cbor::decode(bytes);
(*tree)->get<cbor::type::array>()->size();   // 2
cbor::decode("\x01\x02"s);                   // the item 1; the byte 02 is not read
```

## See also

[item](item.md) · [encode](encode.md) · [lazy::clone](../lazy/clone.md)
