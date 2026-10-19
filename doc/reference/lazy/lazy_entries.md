# cbor::lazy_entries

```cpp
struct lazy_entries : std::ranges::view_interface<lazy_entries> {
    struct iterator {
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::expected<std::pair<lazy, lazy>, error>;
        using difference_type = std::ptrdiff_t;
        iterator() = default;
        value_type operator*() const;
        iterator &operator++();
        iterator operator++(int);
        bool operator==(iterator const &) const;
        bool operator==(std::default_sentinel_t) const;
    };
    iterator begin() const;
    std::default_sentinel_t end() const;
    std::uint64_t size() const;
    void front() const = delete;
};
```

The pairs of one map, from [entries](entries.md), in the order of the
bytes. It is a forward range, a sized range and a borrowed range.
`size()` gives the count of pairs in the head. `*it` gives the key in
`first` and the value in `second`, or the error of the pair. An iterator
holds the owner of the bytes, so it stays valid after the lazy and the
view end. `front()` does not compile.

## Errors

`too_little_data` or another error of the bytes, when the iterator
cannot step over a key or a value. Each later position gives that
error. The view gives at most `size()` values.

## Cost

`end`, `size`, `*it`: O(1). `begin`: O(bytes of the first key). `++it`:
O(bytes of the value and of the next key). No allocation.

## Example

```cpp
// bytes: a std::string with {"a": 1, "b": 2, "c": 3}
auto const map = cbor::lazy::take_ownership(std::move(bytes));
auto const view = *map->entries();
view.size();                                         // 3
std::ranges::distance(view | std::views::drop(1));   // 2
```

## See also

[entries](entries.md) · [lazy_elements](lazy_elements.md)
