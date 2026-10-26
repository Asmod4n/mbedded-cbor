# cbor::lazy::size

```cpp
std::expected<std::uint64_t, error> size() const;
```

Gives the count of the head: the elements of an array, the pairs of a
map, or the elements of a typed array. It reads only the head. It does
not read the children.

## Errors

- `not_indexable`: the value is not an array, a map or a typed array.
- `too_little_data`: the count is larger than the bytes that are left.

## Cost

O(1). No allocation.

## Example

```cpp
// bytes: a std::string with {"a": 1, "b": 2}
auto const map = cbor::lazy::take_ownership(std::move(bytes));
map->size();                     // 2
map->at("a")->size().error();    // not_indexable
```

## See also

[empty](empty.md) · [elements](elements.md) · [entries](entries.md)
