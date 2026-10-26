# cbor::lazy_elements

```cpp
struct lazy_elements : std::ranges::view_interface<lazy_elements> {
    struct iterator {
        using iterator_concept = std::forward_iterator_tag;
        using value_type = std::expected<lazy, error>;
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

The elements of one array, from [elements](elements.md). It is a forward
range, a sized range and a borrowed range. `size()` gives the count of
the head. `*it` gives the lazy of the element, or the error of the
element. An iterator holds the owner of the bytes, so it stays valid
after the lazy and the view end. `front()` does not compile.

## Errors

`too_little_data` or another error of the bytes, when the iterator
cannot step over an element. Each later position gives that error. The
view gives at most `size()` values.

## Cost

`begin`, `end`, `size`, `*it`: O(1). `++it`: O(bytes of the element).
No allocation.

## Example

```cpp
// bytes: a std::string with [{"age": 31}, {"age": 20}, {"age": 40}]
auto const people = cbor::lazy::take_ownership(std::move(bytes));
auto const view = *people->elements();
view.size();   // 3
std::ranges::count_if(view, [](std::expected<cbor::lazy, cbor::error> const &e) {
    return e && e->at("age")->get<int>().value_or(0) > 30;
});            // 2
```

## See also

[elements](elements.md) · [lazy_entries](lazy_entries.md)
