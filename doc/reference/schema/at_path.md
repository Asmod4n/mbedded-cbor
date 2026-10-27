# cbor::schema::at_path

```cpp
template <fixed_string Path, std::convertible_to<std::size_t>... Index>
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && Path.view().starts_with('$') &&
             reflection::path_valid<T, Path, 1>() && sizeof...(Index) == reflection::index_arguments<Path>() &&
             value_holds_no_view<typename decltype(reflection::path_result<T, T, Path, 1>())::type>)
static auto at_path(std::string_view const encoded, Index const... indexes);
```

Reads one value at `Path` and returns a `std::expected` of it. It makes
the checks of [path](path.md) and of an accessor in one call and keeps
nothing. It exists only for a value that holds no view into the bytes:
a number, a `bool`, an enum, or a `std::optional` of one.

A path starts with `$`, names a member with `.name` and an element with
`[n]`. `[]` takes its index from the next argument. A text, a list or a
struct needs an [accessor](accessor.md).

## Errors

- `too_little_data`, `incorrect_type`: as [path](path.md).
- `index_out_of_bounds`: an index past the end of a list.
- `unpopulated_table_index`, `syntax_error`: a broken reference.
- A member that `T` does not have, a wrong count of indexes, or a path
  to a text does not compile.

## Cost

O(q + p): q the size of the table prefix, p the count of steps of the
path. No allocation.

## Example

```cpp
// bytes: from schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}})
cbor::packed<car>::at_path<"$.seats">(bytes);                    // 5
cbor::packed<car>::at_path<"$.tires[1].diameter">(bytes);        // 18
cbor::packed<car>::at_path<"$.tires[].diameter">(bytes, 5uz);    // error::index_out_of_bounds
```

## See also

[accessor](accessor.md) · [path](path.md) · [../../advanced/schema.md](../../advanced/schema.md), section 4
