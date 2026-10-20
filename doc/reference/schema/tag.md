# cbor::tag

```cpp
struct tag {
    std::uint64_t number;
};
```

An annotation that gives a struct its tag number:
`struct [[=cbor::tag(1802)]] point { ... };`. `schema` writes the number
as the first item of each record of the struct. A read compares it, so
a message of another struct gives `incorrect_type`. `schema` reads only
`number`.

`structs` takes the same type in `cbor::describe_members<T>::tag`.

## Errors

These do not compile in `schema<T>`:

- a struct in `T` with no tag;
- two structs in `T` with one number;
- a number of a standard type: 0, 1, 2, 3, 24, 64 to 87, 100, 258,
  1004 and 32873;
- a number that the IANA registry calls always invalid: 21334, 65535,
  4294967295 and 18446744073709551615.

## Cost

Compile time. The head of the number takes 1 to 9 bytes in each record.

## Example

```cpp
struct [[=cbor::tag(1802)]] point {
    float x;
    float y;
};
auto const bytes = cbor::schema<point>::encode(point{1, 2});
// the root record starts with d9 07 0a, the head of tag 1802
```

## See also

[encode](encode.md) · [wire](wire.md) · [../../schema.md](../../schema.md), section 2
