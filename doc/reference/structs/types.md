# Member types

```cpp
template <class T>
class structs;
```

```call-seq
cbor::structs<T>::encode(value)              → std::expected<std::string, cbor::error>
cbor::structs<T>::decode(std::move(encoded)) → std::expected<cbor::ref<T>, cbor::error>
```

```example
message: {"id": 4}
call: auto const r = cbor::structs<reading>::encode(reading{4, std::nullopt})
result: 5 bytes: a1 62 69 64 04
call: cbor::lazy::take_ownership(*r)
result: lazy of {"id": 4}
call: auto const s = cbor::structs<std::set<std::uint8_t>>::encode(std::set<std::uint8_t>{3, 1})
result: 6 bytes: d9 01 02 82 01 03
call: cbor::lazy::take_ownership(*s)
result: lazy of 258([1, 3])
call: cbor::structs<reading>::decode(std::move(bytes))
result: ref to reading{4, std::nullopt}
```

`T`, and each member of a struct in `T`, is one of the types in this
table. `encode` writes the item of the right column. `decode` takes only
that item, under its own tag.

| C++ type | CBOR item |
|---|---|
| `bool`, `std::nullptr_t`, `cbor::simple_value` | `true` or `false`, `null`, a simple value |
| an integer of 8 to 64 bits; `__int128` | an integer; a bignum (tag 2, 3) |
| an enum with a fixed underlying type | the integer |
| `float`, `double` | the shortest float that holds the value |
| `std::basic_string<char>`, `std::string_view` | a text string |
| `std::span<std::byte const>` | a byte string |
| `std::vector`, `std::array` of `std::byte`, an integer, `float`, `double` | an RFC 8746 typed array |
| `std::set`, `std::flat_set` | tag 258 on an array |
| `std::chrono::sys_seconds` | tag 1 on an integer |
| `std::chrono::year_month_day` | tag 100 on an integer of days |
| `std::optional<E>` | the item of E, or `null`; no key when empty |
| `std::variant<E...>` | the item of the alternative |
| `std::tuple<E...>`, `std::array<E, N>` | an array of that count |
| a map type; a multimap type | a map; a map to arrays |
| another container | an array |
| `cbor::tagged_item<N, E>` | tag N on the item of E |
| a struct | a map |

## Errors

`decode` gives `incorrect_type` for another item: a `std::vector<int>`
takes a typed array, not an array. An enum with no fixed underlying
type, a `long double` and a struct under `tagged_item` do not compile.

## Cost

As [encode](encode.md) and [decode](decode.md).

## Example

```cpp
struct reading {
    std::uint8_t id;
    std::optional<std::uint8_t> unit;
};
cbor::structs<reading>::encode(reading{4, std::nullopt});   // {"id": 4}
cbor::structs<std::set<std::uint8_t>>::encode({3, 1});       // 258([1, 3])
```

## See also

[encode](encode.md) · [decode](decode.md) · [../../advanced/structs.md](../../advanced/structs.md), section 3
