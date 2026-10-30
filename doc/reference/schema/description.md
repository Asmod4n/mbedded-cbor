# cbor::schema::description, description_place

```cpp
enum class description_place { inside, apart };

static constexpr std::span<char const> description();
```

```call-seq
cbor::schema<T>::description()                                   → std::span<char const>
cbor::schema<T>::encode<cbor::description_place::apart>(value)   → std::expected<std::string, cbor::error>
cbor::schema<T>::path<cbor::description_place::apart>(std::move(data)) → std::expected<cbor::schema<T>::accessor<T>, cbor::error>
cbor::schema<T>::at_path<"$.member", cbor::description_place::apart>(data) → std::expected<V, cbor::error>
```

```example
bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: auto const data = cbor::schema<car>::encode<cbor::description_place::apart>(car{"Volvo", 5, std::vector<tire>{tire{17, 2.5f}, tire{18, 2.25f}}, std::vector<double>{1.0, 2.0, 3.0}})
result: 130 bytes: d8 71 82 9a 00 00 00 10 5a 00 00 00 0c 00 00 00 25 00 00 00 2f 00 00 00 45 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: data->size()
result: 130
call: cbor::schema<car>::description().size()
result: 76
call: bytes.size()
result: 206
call: cbor::schema<car>::at_path<"$.seats", cbor::description_place::apart>(*data)
result: 5
call: cbor::schema<car>::at_path<"$.seats">(*data)
result: unexpected cbor::error::too_little_data
call: auto const owner = std::make_shared<std::string const>(*data)
result: shared_ptr to 130 bytes: d8 71 82 9a 00 00 00 10 5a 00 00 00 0c 00 00 00 25 00 00 00 2f 00 00 00 45 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: auto const root = cbor::schema<car>::path<cbor::description_place::apart>(owner)
result: accessor of a car
call: root->at_path<"$.tires[1].diameter">()
result: 18
```

`description()` gives the table entries that are the same in every
message of `T`: the 114 entry of each struct and the description of each
element type of a vector of fixed-size structs ([wire](wire.md)). The
bytes are a CBOR sequence (RFC 8742). The compiler makes them.

Each of `encode`, `path`, `at_path`, `decode` and the unverified forms
takes `description_place` as a template argument. The default is
`inside`: the message holds the description. With `apart`, `encode`
writes the message without the description, and the read forms read
such a message. Use it to store the description once and the data of
each key in another place, for example in two named databases of LMDB.

```cpp
template <description_place D = description_place::inside, ...> static ... encode(T const &value, ...);
template <description_place D = description_place::inside, ...> static ... path(...);
template <fixed_string Path, description_place D = description_place::inside, ...> static auto at_path(...);
template <description_place D = description_place::inside, ...> static ... decode(...);
```

The data form is one well-formed CBOR item: tag 113 on the table without
the description and the rump. The count of the table counts only the
entries that it holds. The references keep the indices of the message
with the description, so only `cbor::schema<T>` with `apart` reads the
data form. A generic decoder of Packed CBOR does not.

The size of the data form plus the size of `description()` is the size
of the message.

## Errors

- `incorrect_type`: a message with the description read with `apart`,
  or the data form read with `inside`.
- `too_little_data`: the data form read with `inside`, when it is shorter
  than the least message of `T`.
- Every other error of [path](path.md), [at_path](at_path.md) and
  [decode](decode.md).

## Cost

As [encode](encode.md), [path](path.md), [at_path](at_path.md) and
[decode](decode.md). `description()` costs nothing at run time.

## Example

```cpp
constexpr auto apart = cbor::description_place::apart;
auto const data = cbor::schema<car>::encode<apart>(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}});
auto const description = cbor::schema<car>::description();
data->size() + description.size();                          // the size of the message
cbor::schema<car>::at_path<"$.seats", apart>(*data);        // 5
cbor::schema<car>::at_path<"$.seats">(*data);               // error::incorrect_type or error::too_little_data
cbor::schema<car>::path<apart>(std::string(*data))->at_path<"$.tires[1].diameter">();   // 18
```

## See also

[encode](encode.md) · [path](path.md) · [at_path](at_path.md) ·
[decode](decode.md) · [wire](wire.md)
