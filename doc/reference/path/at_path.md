# cbor::at_path

```cpp
template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> || std::is_same_v<T, bool> ||
              std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
              || std::is_same_v<T, std::float128_t>
#endif
              ))
std::expected<T, error> at_path(std::string_view encoded);

template <fixed_string Path, class T, borrowed_range_of_chars Encoded>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path(std::shared_ptr<void const> owner, Encoded const &encoded);

template <fixed_string Path, class T, owning_range_of_chars Encoded>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path(std::shared_ptr<Encoded const> owner);

template <fixed_string Path, class T, owning_range_of_chars Encoded>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path(Encoded &&encoded);

template <fixed_string Path, copied_value T>
    requires is_singular_query_v<Path>
std::expected<T, error> at_path(std::string_view encoded);

template <scalar_result T>
std::expected<T, error> at_path(std::string_view const encoded, jsonpath::query<T> const &e);

template <view_result T, borrowed_range_of_chars Encoded>
std::expected<ref<T>, error> at_path(std::shared_ptr<void const> owner, Encoded const &encoded,
                                     jsonpath::query<T> const &e);

template <class T, borrowed_range_of_chars Encoded>
    requires(std::is_same_v<T, lazy> || std::is_same_v<T, nodelist>)
std::expected<T, error> at_path(std::shared_ptr<void const> owner, Encoded const &encoded,
                                jsonpath::query<T> const &e);

template <class T, owning_range_of_chars Encoded>
    requires(view_result<T> || std::is_same_v<T, lazy> || std::is_same_v<T, nodelist>)
std::expected<typename jsonpath::query<T>::result_type, error> at_path(std::shared_ptr<Encoded const> owner,
                                                                       jsonpath::query<T> const &e);

template <class T, owning_range_of_chars Encoded>
    requires(view_result<T> || std::is_same_v<T, lazy> || std::is_same_v<T, nodelist>)
std::expected<typename jsonpath::query<T>::result_type, error> at_path(Encoded &&encoded,
                                                                       jsonpath::query<T> const &e);
```

Reads the value at a JSONPath (RFC 9535) in `encoded`, with no decode of
the rest. The first five forms take the path as a string literal, and the
compiler checks it. The other forms take a [query](query.md) that
[compile](compile.md) made at run time.

A number, `bool`, `std::nullptr_t` or `simple_value` comes back as a value
from a `std::string_view`. A text, a byte string or a typed array comes
back as a view in a `ref<T>`, and the `ref` holds the owner of the bytes.
`T` `std::string` or `std::vector<std::byte>` copies the value out of
the bytes. A `lazy` or a [nodelist](nodelist.md) comes only from a query.
No form copies the input bytes. The `Encoded &&` forms move the bytes
into a new owner.

## Errors

`key_not_found`, `index_out_of_bounds`, `not_indexable`,
`incorrect_type`, `number_out_of_range`, `too_little_data`,
`syntax_error`, `indefinite_length` and the limit errors, as
[../../advanced/path.md](../../advanced/path.md), section 10, states. A
path that is not valid or not singular does not compile. A null or
empty owner and a moved-from query throw `std::logic_error`.

## Cost

O(n): the bytes from the start up to the target. No allocation for a
scalar or a view from an owner. A tag 29 on the way, or a key that is not
a text, reads through a lazy and allocates. The `Encoded &&` forms
allocate the owner once. A nodelist allocates its vector.

## Example

```cpp
// bytes: a std::string with {"user": {"id": 42, "name": "ann"}}
auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);          // 42
auto const owner = std::make_shared<std::string const>(std::move(bytes));
auto const name = cbor::at_path<"$.user.name", std::string_view>(owner);  // **name: "ann"
auto const q = cbor::jsonpath::compile<std::int64_t>("$.user.id");
if (q)
    use(cbor::at_path(*owner, *q));                                       // 42
```

## See also

[compile](compile.md) · [nodelist](nodelist.md) · [../lazy/index.md](../lazy/index.md) · [../../path.md](../../path.md)
