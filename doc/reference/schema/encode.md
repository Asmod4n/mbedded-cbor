# cbor::schema::encode

```cpp
static cbor::result<std::string> encode(T const &value)
    requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>());

template <class Target>
    requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
static cbor::result<std::size_t> encode(T const &value, Target &&target);
```

Writes `value` as one Packed CBOR message ([wire](wire.md)). The first
form returns a new `std::string`. The second form appends to `target`
and returns the count of bytes of the message. `target` is a
`std::string`, a container of `char`, a `std::span<char>` or a message
writer. Only public members are written.

Each field has a head of fixed width. So a value does not change the
offset of a field.

## Errors

- `no_buffer_space`: a `std::span<char>` is shorter than the message.
- `value_too_large`: the message is longer than 2^32 - 1 bytes.
- An error of the message writer comes back as its `cbor::error`.
- A struct with no [tag](tag.md), two structs with one tag, or a tag of
  a standard type does not compile.

## Cost

O(i + n), i the count of items of the value, n the size of the message.
The first form makes one `std::string`. A container grows once. A span
allocates nothing.

## Example

```cpp
auto const bytes = cbor::schema<tire>::encode(tire{17, 2.5f});
std::string out = "x";
auto const n = cbor::schema<tire>::encode(tire{17, 2.5f}, out);
use(out.size() == 1 + *n);   // true
```

## See also

[decode](decode.md) · [wire](wire.md) · [../../schema.md](../../schema.md), section 1
