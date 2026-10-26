# cbor::allow

```cpp
struct allow {};
```

`[[=cbor::allow{}]]` on a member of a `cbor::schema` struct with
[allowlist](allowlist.md) keeps that member. On a struct with no
`allowlist`, it changes nothing. It works only with reflection.

`cbor::structs` does not read the annotation.

## Errors

None at run time. The annotation on a type that `cbor::structs` reads
does not compile.

## Cost

None at run time.

## Example

```cpp
struct [[=cbor::tag(1803)]] [[=cbor::allowlist{}]] login {
    [[=cbor::allow{}]] std::uint8_t id;
    std::uint8_t secret;
};
auto bytes = cbor::schema<login>::encode(login{7, 42});   // the key "id", no key "secret"
```

## See also

[allowlist](allowlist.md) · [skip](skip.md) · [../../advanced/schema.md](../../advanced/schema.md), section 2
