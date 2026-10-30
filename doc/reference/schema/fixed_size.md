# cbor::schema::fixed_size

```cpp
template <class U = T>
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
static consteval std::size_t fixed_size();
```

```call-seq
cbor::schema<T>::fixed_size()    → std::size_t
cbor::schema<T>::fixed_size<U>() → std::size_t
```

```example
bytes: d8 71 82 9a 00 00 00 10 d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 5a 00 00 00 00 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 d9 07 08 d8 80 9a 00 00 00 02 19 00 11 fa 40 20 00 00
call: cbor::schema<tire>::fixed_size()
result: 18
call: cbor::schema<tire>::fixed_size<std::uint16_t>()
result: 3
call: cbor::schema<tire>::fixed_size<double>()
result: 9
call: cbor::schema<car>::fixed_size<std::string>()
result: 6
call: bytes.size()
result: 66
call: bytes[48]
result: '\xd9'
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
