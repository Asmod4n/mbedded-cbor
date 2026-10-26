# cbor::schema::fixed_size

```cpp
template <class U = T>
    requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
static consteval std::size_t fixed_size();
```

Gives the count of bytes that a field of type `U` takes in a message of
`T`. With no argument, it gives the size of the root record of `T`. The
root record is the end of the message.

A number takes its head and its argument of fixed width. A struct takes
its class tag, its table reference, a record head of 5 bytes and its
fields. A text, a list, a map or an optional of variable size takes a
reference `6(N)` of 6 bytes.

## Errors

None. A type that has no fixed size, for example a struct with a base
class, does not compile.

## Cost

Compile time. No cost at run time.

## Example

```cpp
cbor::schema<tire>::fixed_size();                  // 18
cbor::schema<tire>::fixed_size<std::uint16_t>();   // 3
cbor::schema<car>::fixed_size<std::string>();      // 6
```

## See also

[member_offset](member_offset.md) · [wire](wire.md)
