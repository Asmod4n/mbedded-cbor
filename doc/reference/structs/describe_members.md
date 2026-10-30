# cbor::describe_members

```cpp
template <class T>
struct describe_members;

template <class T>
concept has_describe_members = requires { describe_members<T>::members; };
```

A specialization describes how `cbor::structs` and `cbor::schema`
write and read `T`. It
holds `tag`, a `std::optional<cbor::tag>`, and `members`, a `std::tuple`
of [member](member.md) entries. `cbor::untagged` gives a map with no
tag. The specialization stands outside every namespace, after `T`.

A class that is not an aggregate, or that has a base class, needs a
description. `cbor::structs` then reads the members, the keys and the
tag from the description only.

## Errors

- `decode` gives `incorrect_type` for a missing tag or another tag.
- A description does not compile when it lists a member twice, gives two
  members one key, names a member of another class, or has no `tag`.
- A tag of a standard type or an invalid tag does not compile.
- With reflection, a list that differs from the public data members does
  not compile. An annotation on `T` does not compile.

## Cost

None at run time. The checks run where `encode` or `decode` compiles.

## Example

```cpp
template <>
struct cbor::describe_members<header> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{160060};
    static constexpr std::tuple members{
        cbor::member{"content-type", &header::content_type},
    };
};
cbor::structs<header>::encode(header{"text/plain"});   // 160060({"content-type": "text/plain"})
```

`CBOR_DEFINE_TYPE(T, TAG, members...)` writes this specialization:

```cpp
CBOR_DEFINE_TYPE(trailer, cbor::tag{160061}, cbor::member{"checksum", &trailer::checksum});
cbor::structs<trailer>::encode(trailer{7});   // 160061({"checksum": 7})
```

The macro stands at global scope. A type whose name holds a comma goes
through an alias.

## See also

[member](member.md) · [CBOR_MEMBER](CBOR_MEMBER.md) · [../../advanced/structs.md](../../advanced/structs.md), section 4
