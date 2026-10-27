# cbor::schema::description, description_place

```cpp
enum class description_place { inside, apart };

static constexpr std::span<char const> description();
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
auto const data = cbor::packed<car>::encode<apart>(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}});
auto const description = cbor::packed<car>::description();
data->size() + description.size();                          // the size of the message
cbor::packed<car>::at_path<"$.seats", apart>(*data);        // 5
cbor::packed<car>::at_path<"$.seats">(*data);               // error::incorrect_type or error::too_little_data
cbor::packed<car>::path<apart>(std::string(*data))->at_path<"$.tires[1].diameter">();   // 18
```

## See also

[encode](encode.md) · [path](path.md) · [at_path](at_path.md) ·
[decode](decode.md) · [wire](wire.md)
