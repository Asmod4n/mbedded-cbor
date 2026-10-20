# cbor::typed_array_view

```cpp
template <class E>
class typed_array_view {
public:
    class iterator;   // random access; operator* returns E
    std::size_t size() const;
    bool empty() const;
    template <class Self> requires std::is_lvalue_reference_v<Self> iterator begin(this Self &&self);
    template <class Self> requires std::is_lvalue_reference_v<Self> iterator end(this Self &&self);
    iterator cbegin() const;
    iterator cend() const;
    std::reverse_iterator<iterator> rbegin() const;
    std::reverse_iterator<iterator> rend() const;
    std::reverse_iterator<iterator> crbegin() const;
    std::reverse_iterator<iterator> crend() const;
    E front() const;
    E back() const;
    cbor::result<typed_array_view> first(std::size_t const n) const &;
    cbor::result<typed_array_view> first(std::size_t const n) &&;
    cbor::result<typed_array_view> last(std::size_t const n) const &;
    cbor::result<typed_array_view> last(std::size_t const n) &&;
    cbor::result<E> at(std::size_t const i) const;
    E operator[](std::size_t const i) const;
};
```

A random access, sized range over an RFC 8746 typed array in the
message. [accessor::view](accessor.md) makes it. It holds the owner of
the bytes. Each element is a value of `E`, read from little endian, not
a reference.

## Errors

- `at`, `first`, `last`: `index_out_of_bounds` as a value.
- `operator[]` outside the view, `front` and `back` on an empty view
  throw `std::logic_error`.
- `begin` and `end` on a temporary do not compile.

## Cost

O(1) for each member. No allocation.

## Example

```cpp
// bytes: from schema<car>::encode(car{..., {1.0, 2.0, 3.0}}), track is std::vector<double>
auto const root = cbor::schema<car>::path(std::move(bytes));
auto const track = root->view<"$.track">();
std::accumulate(track->begin(), track->end(), 0.0);   // 6.0
(*track)[1];                                          // 2.0
track->at(3);                                         // error::index_out_of_bounds
track->last(2)->front();                              // 2.0
```

## See also

[accessor](accessor.md) · [wire](wire.md) · [../../advanced/schema.md](../../advanced/schema.md), section 6
