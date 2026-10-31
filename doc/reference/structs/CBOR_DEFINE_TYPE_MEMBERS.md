# CBOR_DEFINE_TYPE_MEMBERS

```cpp
#define CBOR_DEFINE_TYPE_MEMBERS(T, TAG, ...) CBOR_DEFINE_TYPE(T, TAG, CBOR_MEMBERS(T, __VA_ARGS__))
```

```call-seq
CBOR_DEFINE_TYPE_MEMBERS(device, cbor::untagged, name, port)
```

```example
message: {"name": "lamp", "port": 3}
call: cbor::has_describe_members<device>
result: true
call: auto const encoded = cbor::structs<device>::encode(device{"lamp", 3})
result: 17 bytes: a2 64 6e 61 6d 65 64 6c 61 6d 70 64 70 6f 72 74 03
call: cbor::lazy::take_ownership(*encoded)
result: lazy of {"name": "lamp", "port": 3}
```

Writes the [describe_members](describe_members.md) specialization of
`T` with the tag `TAG` and one [CBOR_MEMBER](CBOR_MEMBER.md) entry for
each name in the list. The key of each member is its name. The list
takes up to 343 names. The macro stands outside every namespace, after
`T`. A type whose name holds a comma needs a type alias.

The macro expands each name before it makes the key. A name that is
also an object-like macro gives the key of what the macro expands to.

## Errors

- A name that is not a member of `T` does not compile.
- A list of more than 343 names does not compile.
- The errors of [describe_members](describe_members.md) apply.

## Cost

None at run time.

## Example

```cpp
struct device {
    std::string name;
    std::uint8_t port;
};

CBOR_DEFINE_TYPE_MEMBERS(device, cbor::untagged, name, port);

cbor::structs<device>::encode(device{"lamp", 3});   // {"name": "lamp", "port": 3}
```

## See also

[CBOR_MEMBER](CBOR_MEMBER.md) · [member](member.md) · [describe_members](describe_members.md)
