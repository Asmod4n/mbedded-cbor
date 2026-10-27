# cbor::tag

```cpp
struct tag {
    std::uint64_t number;
};
```

The tag number of a struct, in its description:
`static constexpr std::optional<cbor::tag> tag = cbor::tag{1802};` in
[cbor::describe_members](../structs/describe_members.md)`<point>`.
`schema` writes the number as the first item of each record of the
struct. A read compares it, so a message of another struct gives
`incorrect_type`. `structs` takes the same description.

## Errors

These do not compile in `schema<T>`:

- a struct in `T` with no description, or with `cbor::untagged`;
- two structs in `T` with one number;
- a number of a standard type: 0, 1, 2, 3, 24, 64 to 87, 100, 258,
  1004 and 32873;
- a number that the IANA registry calls always invalid: 21334, 65535,
  4294967295 and 18446744073709551615.

## Cost

Compile time. The head of the number takes 1 to 9 bytes in each record.

## Example

```cpp
struct point {
    float x;
    float y;
};

template <>
struct cbor::describe_members<point> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1802};
    static constexpr std::tuple members{CBOR_MEMBER(point, x), CBOR_MEMBER(point, y)};
};
auto const bytes = cbor::schema<point>::encode(point{1, 2});
// the root record starts with d9 07 0a, the head of tag 1802
```

## See also

[encode](encode.md) · [wire](wire.md) · [../../schema.md](../../schema.md), section 2
