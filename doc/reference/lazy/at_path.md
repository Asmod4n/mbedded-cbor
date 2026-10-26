# cbor::lazy::at_path

```cpp
template <fixed_string Path>
    requires is_valid_path_v<Path>
std::expected<lazy, error> at_path() const;

template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             ((std::integral<T> && !std::is_same_v<T, bool>) || std::is_same_v<T, double> ||
              std::is_same_v<T, bool> || std::is_same_v<T, std::nullptr_t> || std::is_same_v<T, simple_value>
#ifdef __STDCPP_FLOAT128_T__
              || std::is_same_v<T, std::float128_t>
#endif
              ))
std::expected<T, error> at_path() const;

template <fixed_string Path, class T>
    requires(is_singular_query_v<Path> &&
             (std::is_same_v<T, std::string_view> || std::is_same_v<T, std::span<std::byte const>> ||
              std::is_same_v<T, typed_array>))
std::expected<ref<T>, error> at_path() const;

template <fixed_string Path, std::same_as<nodelist> T>
    requires is_valid_path_v<Path>
std::expected<nodelist, error> at_path() const;

template <class E>
    requires is_path_query_v<E>
std::expected<typename E::result_type, error> at_path(E const &e) const;
```

Reads the JSONPath (RFC 9535) `Path` from this lazy as the root `$`.
Without `T` it gives the lazy of the node. With a scalar `T` it reads
the value as [get](get.md) does. A text, a byte string and a typed array
come back as a `ref` that holds the owner. `nodelist` gives every
selected node. The last form applies a query from
`cbor::jsonpath::compile<T>(path)`, which parses the path at run time.

## Errors

- `key_not_found`, `index_out_of_bounds`, `not_indexable`: a step finds
  no node.
- `incorrect_type`, `number_out_of_range`: the node does not read as `T`.
- `nesting_depth_exceeded`: the path is deeper than the limit.
- A path that is not valid does not compile. A path that can select more
  than one node does not compile, except with `nodelist`.
- A query that is empty or moved from throws `std::logic_error`.

## Cost

O(bytes before the target) for a singular path. No allocation, except
`nodelist`, which allocates the nodes.

## Example

```cpp
// bytes: a std::string with {"user": {"id": 42, "tags": ["a", "b"]}}
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
doc->at_path<"$.user.id", std::int64_t>();                 // 42
auto const last = doc->at_path<"$.user.tags[-1]", std::string_view>();
**last;                                                     // "b"
doc->at_path<"$.user.tags[*]", cbor::nodelist>()->size();   // 2
```

## See also

[at](at.md) · [get](get.md) · [../../path.md](../../path.md)
