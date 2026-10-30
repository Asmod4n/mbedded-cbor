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
    std::expected<typed_array_view, error> first(std::size_t const n) const &;
    std::expected<typed_array_view, error> first(std::size_t const n) &&;
    std::expected<typed_array_view, error> last(std::size_t const n) const &;
    std::expected<typed_array_view, error> last(std::size_t const n) &&;
    std::expected<E, error> at(std::size_t const i) const;
    E operator[](std::size_t const i) const;
};
```

```call-seq
view.size()     → std::size_t
view[i]         → E
view.at(i)      → std::expected<E, cbor::error>
view.first(n)   → std::expected<cbor::typed_array_view<E>, cbor::error>
view.last(n)    → std::expected<cbor::typed_array_view<E>, cbor::error>
view.front()    → E
view.back()     → E
view.begin()    → iterator
```

```example
bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: auto const root = cbor::schema<car>::path(std::move(bytes))
result: accessor of a car
call: auto const track = root->view<"$.track">()
result: typed_array_view<double> of 3 elements: 1, 2, 3
call: std::accumulate(track->begin(), track->end(), 0.0)
result: 6
call: (*track)[1]
result: 2
call: track->at(2)
result: 3
call: track->last(2)->front()
result: 2
call: track->first(2)->size()
result: 2
call: (*track)[2]
result: 3
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
