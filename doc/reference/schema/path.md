# cbor::schema::path

```cpp
template <owning_range_of_chars Encoded>
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
static std::expected<accessor<>, cbor::error> path(Encoded &&encoded);

template <owning_range_of_chars Encoded>
static std::expected<accessor<>, cbor::error> path(std::shared_ptr<Encoded const> encoded)
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>());

template <borrowed_range_of_chars Encoded>
static std::expected<accessor<>, cbor::error> path(std::shared_ptr<void const> owner, Encoded const &encoded)
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>());
```

Opens a message and gives the root [accessor](accessor.md). An rvalue
moves into a new owner. An owner is read in place. Nothing is copied.
An lvalue `std::string` does not compile.

`path` checks the length, the table, the directory and the tag of the
root once. It reads no field.

## Errors

- `too_little_data`: the message is shorter than its table and root, or
  its end is missing.
- `incorrect_type`: a message of another layout, or a wrong tag of the
  root.
- A null or empty owner throws `std::logic_error`.

## Cost

O(1) in the size of the message. The rvalue form makes one
`std::shared_ptr`.

## Example

```cpp
// bytes: a std::string from schema<car>::encode(car{"Volvo", 5, ...})
auto const root = cbor::packed<car>::path(std::move(bytes));
if (root)
    use(root->at_path<"$.make">());   // "Volvo"
```

## See also

[accessor](accessor.md) · [at_path](at_path.md) · [decode](decode.md) · [../../schema.md](../../schema.md), section 4
