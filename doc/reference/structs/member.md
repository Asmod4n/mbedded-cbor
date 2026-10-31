# cbor::member

```cpp
template <class P>
struct member {
    static_assert(std::is_member_object_pointer_v<P>, "cbor::member takes a non-static data member only.");
    map_key key;
    P pointer;
};
```

```call-seq
cbor::member{0, &senml::n}       → cbor::member<std::string senml::*>
cbor::member{"name", &person::name} → cbor::member<std::string person::*>
```

```example
bytes: a2 00 61 74 02 f9 3e 00
call: auto const encoded = cbor::structs<senml>::encode(senml{"t", 1.5})
result: 8 bytes: a2 00 61 74 02 f9 3e 00
call: cbor::lazy::take_ownership(*encoded)
result: lazy of {0: "t", 2: 1.5}
```

One entry of [describe_members](describe_members.md): the key of a
member and a pointer to that member. The key is a text literal or an
integer from -2^63 to 2^64 - 1. The text is the name as it is written.

## Errors

A pointer to a static member or to a member function does not compile.
A `bool` or a character type as the key does not compile.

## Cost

None at run time.

## Example

```cpp
template <>
struct cbor::describe_members<senml> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        cbor::member{0, &senml::n},
        cbor::member{2, &senml::v},
    };
};
cbor::structs<senml>::encode(senml{"t", 1.5});   // {0: "t", 2: 1.5}
```

## See also

[CBOR_MEMBER](CBOR_MEMBER.md) · [CBOR_DEFINE_TYPE_MEMBERS](CBOR_DEFINE_TYPE_MEMBERS.md) · [describe_members](describe_members.md) · [../../advanced/structs.md](../../advanced/structs.md), section 4
