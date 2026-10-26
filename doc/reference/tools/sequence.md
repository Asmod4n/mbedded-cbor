# cbor::sequence

```cpp
class sequence
{
public:
    template <owning_range_of_chars Encoded>
    explicit sequence(std::shared_ptr<Encoded const> e);

    template <borrowed_range_of_chars Encoded>
    sequence(std::shared_ptr<void const> o, Encoded const &e);

    struct iterator {
        using value_type = std::expected<std::string_view, error>;
        using difference_type = std::ptrdiff_t;

        value_type operator*() const;
        iterator &operator++();
        void operator++(int);
        bool operator==(std::default_sentinel_t) const;
    };

    iterator begin() const;
    std::default_sentinel_t end() const;
    bool empty() const;
};
```

Splits a CBOR sequence (RFC 8742), items one after the other, into its
items. Each item is a `std::string_view` into the bytes, with no copy.
The sequence holds the owner, so a view stays valid while the sequence or
the owner lives. Read an item with `at_path` or `lazy::view(owner, *item)`.

The sequence is an input range: it has no size, and `begin()` reads the
first item again. [item_size](item_size.md) finds the end of each item.

## Errors

Each item gives the errors of [item_size](item_size.md). After an item
with an error, the next `++` ends the loop, because the end of that item
is not known. A null `std::shared_ptr` or an empty owner throws
`std::logic_error`.

## Cost

The constructor is O(1) and reads nothing. Each `++` and `begin()` is
O(n_item). Memory O(1). No allocation.

## Example

```cpp
// owner: a std::shared_ptr<std::string const> with the sequence 1, 2, 3
std::int64_t sum = 0;
for (auto const item : cbor::sequence(owner))
    if (item)
        sum += cbor::at_path<"$", std::int64_t>(*item).value_or(0);
use(sum);   // 6
```

## See also

[item_size](item_size.md) · [../lazy/index.md](../lazy/index.md) · [../../advanced/basics.md](../../advanced/basics.md), section 8
