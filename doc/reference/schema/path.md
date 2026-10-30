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

```call-seq
cbor::schema<T>::path(std::move(encoded)) → std::expected<cbor::schema<T>::accessor<T>, cbor::error>
cbor::schema<T>::path(owner)              → std::expected<cbor::schema<T>::accessor<T>, cbor::error>
cbor::schema<T>::path(owner, encoded)     → std::expected<cbor::schema<T>::accessor<T>, cbor::error>
```

```example
bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: cbor::schema<tire>::path(std::string(bytes)).error()
result: cbor::error::incorrect_type
call: auto const root = cbor::schema<car>::path(std::move(bytes))
result: accessor of a car
call: root->at_path<"$.make">()
result: "Volvo"
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
auto const root = cbor::schema<car>::path(std::move(bytes));
if (root)
    use(root->at_path<"$.make">());   // "Volvo"
```

## See also

[accessor](accessor.md) · [at_path](at_path.md) · [decode](decode.md) · [../../schema.md](../../schema.md), section 4
