# cbor::lazy::entries

```cpp
std::expected<lazy_entries, error> entries() const;
```

Gives the pairs of a map, in the order of the bytes, as a view. Each
element of the view is a `std::expected<std::pair<lazy, lazy>, error>`:
the key in `first`, the value in `second`. A key that occurs twice
occurs twice in the view.

## Errors

- `not_indexable`: the value is not a map.
- `indefinite_length`: the map has an indefinite length.
- `too_little_data`: the count is larger than the bytes that are left.

## Cost

O(1). No allocation. A full loop is O(bytes of the map).

## Example

```cpp
// bytes: a std::string with {"a": 1, "b": 2}
auto const map = cbor::lazy::take_ownership(std::move(bytes));
for (auto const pair : *map->entries()) {
    auto const key = pair->first.get<std::string_view>();
    use(**key, *pair->second.get<int>());   // "a" 1, then "b" 2
}
```

## See also

[lazy_entries](lazy_entries.md) · [elements](elements.md) · [at](at.md)
