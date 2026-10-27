# cbor::schema::unverified_path, unverified_at_path, unverified_decode

```cpp
template <owning_range_of_chars Encoded>
static std::expected<accessor<T, false>, error> unverified_path(Encoded &&encoded);
template <owning_range_of_chars Encoded>
static std::expected<accessor<T, false>, error> unverified_path(std::shared_ptr<Encoded const> encoded);
template <borrowed_range_of_chars Encoded>
static std::expected<accessor<T, false>, error> unverified_path(std::shared_ptr<void const> owner, Encoded const &encoded);

template <fixed_string Path, std::convertible_to<std::size_t>... Index>
static auto unverified_at_path(std::string_view const encoded, Index const... indexes);

template <owning_range_of_chars Encoded>
static std::expected<ref<T>, error> unverified_decode(Encoded &&encoded);
template <owning_range_of_chars Encoded>
static std::expected<ref<T>, error> unverified_decode(std::shared_ptr<Encoded const> encoded);
template <owning_range_of_chars Encoded>
static std::expected<ref<T>, error> unverified_decode(std::shared_ptr<Encoded const> encoded, T target);
template <borrowed_range_of_chars Encoded>
static std::expected<ref<T>, error> unverified_decode(std::shared_ptr<void const> owner, Encoded const &encoded);
template <borrowed_range_of_chars Encoded>
static std::expected<ref<T>, error> unverified_decode(std::shared_ptr<void const> owner, Encoded const &encoded, T target);
```

Each function takes the arguments of [path](path.md),
[at_path](at_path.md) or [decode](decode.md) and gives the same result
for a message that `encode` wrote. It does not check the data of the
message. It checks every bound, so no read leaves the input. Use it for a
message from a peer that you trust.

`unverified_path` gives an `accessor<T, false>`. Every accessor that it
gives on a path is unverified too, and so is each element of a vector of
fixed-size structs. `view` of an unverified accessor does not check the
tag of the typed array.

## What is not checked

- the tag of each struct;
- the head of a number, a `bool`, a fixed string and a `std::array`;
- the head byte of a text, a list and a map, and the tag of a typed array;
- the prefix of a vector of fixed-size structs;
- the start of the table and the fillers before the items;
- that the length of a typed array is a multiple of its element size;
- that a `bool` byte is 0 or 1: any other byte reads as `true`;
- in `unverified_decode`, the order of the references and of the items.

## What is checked

- that each head, length and item lies inside the message;
- that each directory entry lies between the start and the end of the
  items;
- each index against the count of its list, its table or its directory;
- the limits of the build and, in `unverified_decode`, the nesting depth.

## Errors

- `too_little_data`: a message that ends too early, or a length past the
  end.
- `index_out_of_bounds`: an index past the end of a list.
- `unpopulated_table_index`, `syntax_error`: a reference or a directory
  entry outside the table or the items.
- A wrong byte of data gives a value, not an error.

## Cost

As [path](path.md), [at_path](at_path.md) and [decode](decode.md),
without the checks above.

## Example

```cpp
// bytes: from schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}})
cbor::packed<car>::unverified_at_path<"$.seats">(bytes);                  // 5
cbor::packed<car>::unverified_at_path<"$.tires[].diameter">(bytes, 5uz);  // error::index_out_of_bounds

auto const lane = cbor::packed<car>::unverified_path(std::string(bytes));
lane->at_path<"$.tires[1].diameter">();                                   // 18
```

## See also

[path](path.md) · [at_path](at_path.md) · [decode](decode.md) · [accessor](accessor.md) ·
[../../advanced/schema.md](../../advanced/schema.md), section 7
