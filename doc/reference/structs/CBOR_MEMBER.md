# CBOR_MEMBER

```cpp
#define CBOR_MEMBER(T, m) cbor::member{cbor::map_key(std::string_view{#m}), &T::m}
```

```call-seq
CBOR_MEMBER(account, user) → cbor::member<std::string account::*>
```

```example
message: {"user": "ann", "level": 2}
call: auto const encoded = cbor::structs<account>::encode(account{"ann", 2})
result: 17 bytes: a2 64 75 73 65 72 63 61 6e 6e 65 6c 65 76 65 6c 02
call: cbor::lazy::take_ownership(*encoded)
result: lazy of {"user": "ann", "level": 2}
```

Makes the [member](member.md) entry of `T::m` with the text of the name
`m` as its key. A type whose name holds a comma needs a type alias.

## Errors

As [member](member.md).

## Cost

None at run time.

## Example

```cpp
template <>
struct cbor::describe_members<account> {
    static constexpr std::optional<cbor::tag> tag = cbor::untagged;
    static constexpr std::tuple members{
        CBOR_MEMBER(account, user),
        CBOR_MEMBER(account, level),
    };
};
cbor::structs<account>::encode(account{"ann", 2});   // {"user": "ann", "level": 2}
```

## See also

[member](member.md) · [describe_members](describe_members.md) · [CBOR_DEFINE_TYPE_MEMBERS](CBOR_DEFINE_TYPE_MEMBERS.md)
