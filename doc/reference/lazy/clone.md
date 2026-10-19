# cbor::lazy::clone

```cpp
std::expected<owning<item>, error> clone() const;
```

Decodes the whole item at the lazy into a tree of
[cbor::item](../item/item.md), as [cbor::decode](../item/decode.md) does.
The tree holds a copy of each text and byte string. It does not view the
bytes of the lazy, and stays valid after the lazy and its owner end.

## Errors

The errors of [cbor::decode](../item/decode.md): `too_little_data`,
`syntax_error`, `indefinite_length`, `nesting_depth_exceeded` and
others for a broken item.

## Cost

O(bytes of the item). The tree takes one memory zone, which grows in
chunks, and one `std::shared_ptr`.

## Example

```cpp
// bytes: a std::string with {"a": [1, "x"]}
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
auto const tree = doc->at("a")->clone();
(*tree)->type;                                     // cbor::type::array
(*tree)->get<cbor::type::array>()->size();         // 2
```

## See also

[cbor::item](../item/item.md) · [cbor::decode](../item/decode.md) · [get](get.md)
