# cbor::schema::member_offset

```cpp
template <auto Member, class U = T>
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
static consteval std::size_t member_offset();
```

Gives the offset of the head of `Member` from the start of the record of
`U`. `Member` is a pointer to a described member, `&U::m`. With
reflection, `Member` can also be a reflection of the member, `^^U::m`. The record starts with the class tag, the table reference and the
record head. The fields follow the key order of RFC 8949 4.2.1: a
shorter key first, then the bytes of the key. This order is not the
order of declaration.

The root record of `T` starts at `size - fixed_size()` of the message.

## Errors

None. A `Member` of another struct, or a member that the description
does not list, does not compile.

## Cost

Compile time. No cost at run time.

## Example

```cpp
// tire: std::uint16_t diameter; float pressure;
cbor::packed<tire>::member_offset<&tire::diameter>();   // 10
cbor::packed<tire>::member_offset<&tire::pressure>();   // 13
// bytes[bytes.size() - fixed_size() + 13] is 0xfa, the head of a float
```

## See also

[fixed_size](fixed_size.md) · [wire](wire.md)
