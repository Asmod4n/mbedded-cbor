# cbor::schema::decode

```cpp
template <owning_range_of_chars Encoded>
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
static std::expected<ref<T>, cbor::error> decode(Encoded &&encoded);

template <owning_range_of_chars Encoded>
static std::expected<ref<T>, cbor::error> decode(std::shared_ptr<Encoded const> encoded)
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>());

template <owning_range_of_chars Encoded>
static std::expected<ref<T>, cbor::error> decode(std::shared_ptr<Encoded const> encoded, T target)
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && std::is_default_constructible_v<T>);

template <borrowed_range_of_chars Encoded>
static std::expected<ref<T>, cbor::error> decode(std::shared_ptr<void const> owner, Encoded const &encoded)
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>());

template <borrowed_range_of_chars Encoded>
static std::expected<ref<T>, cbor::error> decode(std::shared_ptr<void const> owner, Encoded const &encoded, T target)
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && std::is_default_constructible_v<T>);
```

```call-seq
cbor::schema<T>::decode(std::move(encoded))       → std::expected<cbor::ref<T>, cbor::error>
cbor::schema<T>::decode(owner)                    → std::expected<cbor::ref<T>, cbor::error>
cbor::schema<T>::decode(owner, target)            → std::expected<cbor::ref<T>, cbor::error>
cbor::schema<T>::decode(owner, encoded)           → std::expected<cbor::ref<T>, cbor::error>
cbor::schema<T>::decode(owner, encoded, target)   → std::expected<cbor::ref<T>, cbor::error>
```

```example
bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: auto const back = cbor::schema<car>::decode(std::move(bytes))
result: ref to car{"Volvo", 5, {tire{17, 2.5f}, tire{18, 2.25f}}, {1, 2, 3}}
call: (*back)->make
result: "Volvo"
```

Reads the whole message into a `T` and returns it in a `cbor::ref<T>`
that holds the owner of the bytes. A `std::string_view` member views
the bytes. Nothing is copied: an rvalue moves into a new owner, and an
owner is read in place. An lvalue `std::string` does not compile.
`target` gives the start value, for example `std::pmr` containers with
their memory resource; the forms with `target` need a default
constructor of `T`.

A struct with a default constructor is made with it, and then each
described member is set. A class with no default constructor is made
by a constructor that takes the described members in the order of the
description: decode reads them into a `std::tuple` and calls
`std::make_from_tuple`. An element of a container with an allocator
makes that tuple with the allocator of the container.

## Errors

- `too_little_data`: the message is shorter than its table and root.
- `incorrect_type`: a message of another struct, or a wrong head.
- `syntax_error`, `unpopulated_table_index`: a broken directory.
- `inadmissible_type_for_tag_content`: a typed array whose length is
  not a multiple of its element size.
- `nesting_depth_exceeded`, `not_enough_memory`.
- A null or empty owner throws `std::logic_error`.

## Cost

O(n), n the size of the message; a `std::map` adds O(k log k). One
allocation for each text, list and node of the result.

## Example

```cpp
// bytes: a std::string from schema<car>::encode(car{"Volvo", 5, ...})
auto const back = cbor::schema<car>::decode(std::move(bytes));
if (back)
    use((*back)->make);   // "Volvo"
```

## See also

[encode](encode.md) · [path](path.md) · [../ref/index.md](../ref/index.md) · [../../advanced/schema.md](../../advanced/schema.md), section 3
