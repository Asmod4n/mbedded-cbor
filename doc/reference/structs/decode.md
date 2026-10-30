# cbor::structs<T>::decode

```cpp
template <owning_range_of_chars Encoded>
static std::expected<cbor::ref<T>, cbor::error> decode(Encoded &&encoded);

template <owning_range_of_chars Encoded>
static std::expected<cbor::ref<T>, cbor::error> decode(std::shared_ptr<Encoded const> encoded);

template <owning_range_of_chars Encoded>
static std::expected<cbor::ref<T>, cbor::error> decode(std::shared_ptr<Encoded const> encoded, T &&target);

template <borrowed_range_of_chars Encoded>
static std::expected<cbor::ref<T>, cbor::error> decode(std::shared_ptr<void const> owner, Encoded const &encoded);

template <borrowed_range_of_chars Encoded>
static std::expected<cbor::ref<T>, cbor::error> decode(std::shared_ptr<void const> owner, Encoded const &encoded,
                                                       T &&target);
```

```call-seq
cbor::structs<T>::decode(std::move(encoded))     → std::expected<cbor::ref<T>, cbor::error>
cbor::structs<T>::decode(owner)                  → std::expected<cbor::ref<T>, cbor::error>
cbor::structs<T>::decode(owner, std::move(target)) → std::expected<cbor::ref<T>, cbor::error>
cbor::structs<T>::decode(owner, encoded)         → std::expected<cbor::ref<T>, cbor::error>
cbor::structs<T>::decode(owner, encoded, std::move(target)) → std::expected<cbor::ref<T>, cbor::error>
```

```example
message: {"name": "ann", "age": 30}
call: auto const p = cbor::structs<person>::decode(std::move(bytes))
result: ref to person{"ann", 30}
call: (*p)->age
result: 30
call: (*p)->name
result: "ann"
```

Reads one item into a `T`. The result is a [ref](../ref/ref.md) that
holds the owner of the bytes, so a `std::string_view` in `T` stays
valid. No form copies the bytes. The first form moves an rvalue into a
new owner; an lvalue does not compile. `owner` keeps the bytes of a
borrowed `encoded` alive. A form with `target` reads into the
containers of `target`, with their allocator. The other forms start
from `T{}`.

## Errors

- `incorrect_type`: an item of another type, a missing or other tag.
- `number_out_of_range`: a number that the type cannot hold.
- `key_not_found`: no key for a member that is not `std::optional`.
- `duplicate_key`: a key that comes twice.
- `too_little_data`: the input ends inside the item. A byte after the
  item is not read. `not_enough_memory`: a refused allocation.
- A null or empty owner throws `std::logic_error`.

## Cost

Common O(n); O((n/d)^d) with tag 29. The first form allocates the
owner. Every form allocates the containers of the result.

## Example

```cpp
// bytes: a std::string with {"name": "ann", "age": 30}
auto const p = cbor::structs<person>::decode(std::move(bytes));
if (p)
    use((*p)->age);   // 30
```

## See also

[encode](encode.md) · [types](types.md) · [../ref/ref.md](../ref/ref.md) · [../../advanced/structs.md](../../advanced/structs.md), sections 2 and 9
