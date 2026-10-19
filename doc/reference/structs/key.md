# cbor::key

```cpp
struct key {
    char const *text = nullptr;
    std::int64_t number = 0;

    template <std::same_as<char> C>
    consteval explicit key(C const *const s);

    constexpr explicit key(std::int64_t const n);
};
```

`[[=cbor::key("x")]]` on a member of a `cbor::schema` struct makes `x`
the key of that member in place of its name. The text form exists only
with reflection. `cbor::key{n}` is an integer key for
`lazy::at` and `lazy::operator[]`.

`cbor::structs` does not read the annotation. A struct that
`cbor::structs` reads takes its keys from its member names or from
[describe_members](describe_members.md).

## Errors

None at run time. The annotation on a type that `cbor::structs` reads
does not compile. Two members with one key in a `cbor::schema` struct
do not compile.

## Cost

None at run time. The key is a constant of the program.

## Example

```cpp
struct [[=cbor::tag(1801)]] renamed {
    [[=cbor::key("id")]] std::uint8_t number;
};
auto const bytes = cbor::schema<renamed>::encode(renamed{7});
if (bytes)
    use(*bytes);   // the key "id", no key "number"
```

## See also

[skip](skip.md) · [member](member.md) · [../../advanced/schema.md](../../advanced/schema.md), section 2
