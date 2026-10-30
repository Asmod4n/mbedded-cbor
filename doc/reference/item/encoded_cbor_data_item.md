# cbor::encoded_cbor_data_item

```cpp
inline std::expected<std::reference_wrapper<item const>, error>
encoded_cbor_data_item(owning<item> const &tree, item const &tagged);
```

```call-seq
cbor::encoded_cbor_data_item(*tree, **tree) → std::expected<std::reference_wrapper<item const>, error>
```

```example
bytes: d8 18 42 61 61
call: auto const tree = cbor::decode(bytes)
result: owning item of 24(h'6161')
call: auto const inner = cbor::encoded_cbor_data_item(*tree, **tree)
result: reference to item of "a"
call: inner->get().get<cbor::type::text_string>()
result: "a"
```

Gives the root of the item that a tag 24 (RFC 8949 3.4.5.1) holds in its
byte string. `tagged` is the tag node in `tree`. [decode](decode.md)
decodes that content when it decodes the tree, so the call only looks it
up.

## Errors

- `incorrect_type`: the content of the tag 24 is not a byte string.
- `tagged` is not a tag 24 of `tree`: the call throws
  `std::logic_error`.

## Cost

O(log t), t the tag 24 nodes of the tree. No allocation.

## Example

```cpp
// bytes: a std::string with 24(h'6161'), a tag 24 over the text "a"
auto const tree = cbor::decode(bytes);
auto const inner = cbor::encoded_cbor_data_item(*tree, **tree);
inner->get().get<cbor::type::text_string>();   // "a"
```

## See also

[decode](decode.md) · [item](item.md)
