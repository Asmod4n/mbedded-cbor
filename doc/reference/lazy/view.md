# cbor::lazy::view

```cpp
template <borrowed_range_of_chars Encoded>
static cbor::result<lazy> view(std::shared_ptr<void const> owner, Encoded const &encoded);
template <owning_range_of_chars Encoded>
static cbor::result<lazy> view(std::shared_ptr<Encoded const> owner);
```

Makes the lazy of the top-level item in the bytes, in place. Nothing is
copied. The first form reads `encoded`, and `owner` keeps those bytes
alive. The second form reads `*owner`. Every lazy, view and `ref` that
comes from the result holds `owner`.

The call does not check that the bytes are one complete item. A step on
the lazy finds a missing part. No step reads bytes after the item.

## Errors

None from the bytes. The call reads only the first head, to skip one
tag 55799. A null `owner` in the second form throws `std::logic_error`.
An empty `owner` in the first form throws `std::logic_error`.

## Cost

O(1). No allocation: the lazy holds a copy of `owner`.

## Example

```cpp
// owner: a std::shared_ptr<std::string const> with {"id": 42}
auto const doc = cbor::lazy::view(owner);
use(doc->at("id")->get<std::int64_t>());   // 42
```

## See also

[take_ownership](take_ownership.md) · [at](at.md) · [lazy](lazy.md)
