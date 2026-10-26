# cbor::allowlist

```cpp
struct allowlist {};
```

`[[=cbor::allowlist{}]]` on a `cbor::schema` struct writes only the
members that carry [allow](allow.md). Every other member is left out as
with [skip](skip.md). It works only with reflection.

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
auto bytes = cbor::schema<login>::encode(login{7, 42});
auto const back = cbor::schema<login>::decode(std::move(*bytes));
if (back)
    use((*back)->id, (*back)->secret);   // 7, 0
```

## See also

[allow](allow.md) · [skip](skip.md) · [../../advanced/schema.md](../../advanced/schema.md), section 2
