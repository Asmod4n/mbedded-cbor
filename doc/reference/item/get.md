# cbor::item::get

```cpp
template <cbor::type K>
cbor::result<item_alternative_t<K>> get() const;
```

Gives the value of the node when `type` is `K`.

| K | Result |
|---|---|
| `unsigned_integer`, `negative_integer` | `std::uint64_t`, the argument |
| `half_float`, `single_float`, `double_float` | `double` |
| `text_string` | `std::string_view` |
| `byte_string` | `std::span<std::byte const>` |
| `array` | `std::span<item const>`, the elements |
| `map` | `std::span<item const>`, each key and its value one after the other |
| `tag`, `unsigned_bignum`, `negative_bignum`, `typed_array` | `std::pair<std::uint64_t, item const *>`: the tag number and the content |
| `false_value`, `true_value`, `null`, `undefined`, `simple_value` | `cbor::simple_value` |

A negative integer gives its argument n, for the value -1 - n. A view
and a pointer in the result point into the tree.

## Errors

`incorrect_type`: `type` is not `K`.

## Cost

O(1). No allocation.

## Example

```cpp
// bytes: a std::string with {"a": -3}
auto const tree = cbor::decode(bytes);
auto const map = *(*tree)->get<cbor::type::map>();
map[0].get<cbor::type::text_string>();          // "a"
map[1].get<cbor::type::negative_integer>();     // 2, for -3
map[1].get<cbor::type::unsigned_integer>().error();   // incorrect_type
```

## See also

[item](item.md) · [decode](decode.md)
