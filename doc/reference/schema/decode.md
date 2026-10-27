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
auto const back = cbor::packed<car>::decode(std::move(bytes));
if (back)
    use((*back)->make);   // "Volvo"
```

## See also

[encode](encode.md) · [path](path.md) · [../ref/index.md](../ref/index.md) · [../../advanced/schema.md](../../advanced/schema.md), section 3
