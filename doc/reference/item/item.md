# cbor::item

```cpp
struct item {
    cbor::type const type;
    std::uint8_t const additional_information;

    template <cbor::type K>
    cbor::result<item_alternative_t<K>> get() const;
};

template <cbor::type K>
using item_alternative_t = typename item_alternative<K>::type;
```

One node of a tree from [decode](decode.md) or
[lazy::clone](../lazy/clone.md). `type` gives the kind of the node.
`additional_information` gives the low 5 bits of the initial byte
(RFC 8949 3). [get](get.md) gives the value.

The constructor is private: only a decode makes an item. An item owns no
memory and has no destructor to run. The tree owns every node and every
string.

## Errors

None. `type` and `additional_information` are data members.

## Cost

O(1). No allocation.

## Example

```cpp
// bytes: a std::string with [1, {"a": 2}]
auto const tree = cbor::decode(bytes);
(*tree)->type;                                  // cbor::type::array
(*tree)->get<cbor::type::array>()->size();      // 2
```

## See also

[get](get.md) · [decode](decode.md) · [encode](encode.md)
