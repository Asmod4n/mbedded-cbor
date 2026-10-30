# cbor::item

```cpp
struct item {
    cbor::type const type;
    std::uint8_t const additional_information;

    template <cbor::type K>
    std::expected<item_alternative_t<K>, error> get() const;
};

template <cbor::type K>
using item_alternative_t = typename item_alternative<K>::type;
```

```call-seq
(*tree)->type                     → cbor::type const
(*tree)->additional_information   → std::uint8_t const
(*tree)->get<cbor::type::array>() → std::expected<std::span<item const>, error>
```

```example
message: [1, {"a": 2}]
call: auto const tree = cbor::decode(bytes)
result: owning item of [1, {"a": 2}]
call: (*tree)->type
result: cbor::type::array
call: (*tree)->additional_information
result: 2
call: (*tree)->get<cbor::type::array>()->size()
result: 2
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

A read of `type`, `additional_information` or [get](get.md) is O(1) and
allocates nothing. The tree comes from [decode](decode.md): it copies
every string of the item and allocates the nodes, O(bytes of the item).

## Example

```cpp
// bytes: a std::string with [1, {"a": 2}]
auto const tree = cbor::decode(bytes);
(*tree)->type;                                  // cbor::type::array
(*tree)->get<cbor::type::array>()->size();      // 2
```

## See also

[get](get.md) · [decode](decode.md) · [encode](encode.md)
