# cbor::tag

```cpp
struct tag {
    std::uint64_t number;
};
```

```call-seq
cbor::tag{number} → cbor::tag
```

```example
bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: cbor::tag{1802}
result: cbor::tag{1802}
call: cbor::schema<point>::encode(point{1, 2})
result: 54 bytes: d8 71 82 9a 00 00 00 10 d8 72 82 61 78 61 79 5a 00 00 00 00 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 d9 07 0a d8 80 9a 00 00 00 02 fa 3f 80 00 00 fa 40 00 00 00
call: cbor::schema<car>::path(std::move(bytes)).has_value()
result: true
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
