# cbor::schema::encode

```cpp
static std::expected<std::string, cbor::error> encode(T const &value)
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>());

template <class Target>
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
static std::expected<std::size_t, cbor::error> encode(T const &value, Target &&target);
```

```call-seq
cbor::schema<T>::encode(value)         → std::expected<std::string, cbor::error>
cbor::schema<T>::encode(value, target) → std::expected<std::size_t, cbor::error>
```

```example
bytes: d8 71 82 9a 00 00 00 10 d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 5a 00 00 00 00 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 d9 07 08 d8 80 9a 00 00 00 02 19 00 11 fa 40 20 00 00
call: cbor::schema<tire>::encode(tire{17, 2.5f})
result: 66 bytes: d8 71 82 9a 00 00 00 10 d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 5a 00 00 00 00 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 d9 07 08 d8 80 9a 00 00 00 02 19 00 11 fa 40 20 00 00
call: std::string out = "x"
result: "x"
call: auto const n = cbor::schema<tire>::encode(tire{17, 2.5f}, out)
result: 66
call: out.size()
result: 67
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
