# cbor::lazy

```cpp
struct lazy {
    top_level_item top_level;
    validity::offset offset;
    std::uint64_t typed_array_tag{};
};

using nodelist = std::vector<lazy>;
```

A lazy is a position in one top-level item. It holds the owner of the
bytes, the bytes and an offset. It holds no decoded value. Each member
function reads the bytes at the offset when it is called.

[view](view.md) and [take_ownership](take_ownership.md) set the data
members. A copy of a lazy is a second position in the same bytes.
Several threads read one lazy at the same time. A `nodelist` is the
result of a path that selects many nodes.

## Errors

A lazy made with `cbor::lazy{}` holds no top-level item. Each member
function then throws `std::logic_error`.

## Cost

A copy is O(1): one atomic increment of the owner. No allocation.

## Example

```cpp
// bytes: a std::string with [1, 2]
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
cbor::lazy const copy = *doc;
use(copy.at(1)->get<int>());   // 2
cbor::lazy{}.size();           // throws std::logic_error
```

## See also

[index](index.md) · [view](view.md) · [../../runtime.md](../../runtime.md)
