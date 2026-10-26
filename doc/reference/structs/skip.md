# cbor::skip

```cpp
struct skip {};
```

`[[=cbor::skip{}]]` on a member of a `cbor::schema` struct leaves that
member out. `encode` writes no key for it, and `decode` leaves its value
from `T{}`. It works only with reflection.

`cbor::structs` does not read the annotation. A struct that
`cbor::structs` reads leaves a member out through
[describe_members](describe_members.md).

## Errors

None at run time. The annotation on a type that `cbor::structs` reads
does not compile.

## Cost

None at run time.

## Example

```cpp
struct [[=cbor::tag(1802)]] guarded {
    std::uint8_t id;
    [[=cbor::skip{}]] std::uint8_t secret;
};
auto bytes = cbor::schema<guarded>::encode(guarded{7, 42});
auto const back = cbor::schema<guarded>::decode(std::move(*bytes));
if (back)
    use((*back)->secret);   // 0
```

## See also

[allowlist](allowlist.md) · [key](key.md) · [../../advanced/schema.md](../../advanced/schema.md), section 2
