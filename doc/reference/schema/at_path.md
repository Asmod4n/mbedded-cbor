# cbor::schema::at_path

```cpp
template <fixed_string Path, std::convertible_to<std::size_t>... Index>
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>() && Path.view().starts_with('$') &&
             reflection::path_valid<T, Path, 1>() && sizeof...(Index) == reflection::index_arguments<Path>() &&
             value_holds_no_view<typename decltype(reflection::path_result<T, T, Path, 1>())::type>)
static auto at_path(std::string_view const encoded, Index const... indexes);
```

```call-seq
cbor::schema<T>::at_path<"$.member">(encoded)         → std::expected<V, cbor::error>
cbor::schema<T>::at_path<"$.list[].member">(encoded, i) → std::expected<V, cbor::error>
```

```example
bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: cbor::schema<car>::at_path<"$.seats">(bytes)
result: 5
call: cbor::schema<car>::at_path<"$.tires[1].diameter">(bytes)
result: 18
call: cbor::schema<car>::at_path<"$.tires[].pressure">(bytes, 0u)
result: 2.5
call: cbor::schema<car>::at_path<"$.track[2]">(bytes)
result: 3
call: cbor::schema<car>::at_path<"$.tires[].diameter">(bytes, 5u)
result: unexpected cbor::error::index_out_of_bounds
call: cbor::schema<car>::at_path<"$.make">(bytes)
result: fault: at_path<"$.make"> with 0 indexes does not compile
```

Reads one value at `Path` and returns a `std::expected` of it. It makes
the checks of [path](path.md) and of an accessor in one call and keeps
nothing. It exists only for a value that holds no view into the bytes:
a number, a `bool`, an enum, or a `std::optional` of one.

A path starts with `$`, names a member with `.name` and an element with
`[n]`. `[]` takes its index from the next argument. On an array or a
vector of `std::uint8_t` or `std::byte`, `[n]` reads one byte. A text, a
list or a struct needs an [accessor](accessor.md).

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
cbor::schema<car>::at_path<"$.seats">(bytes);                    // 5
cbor::schema<car>::at_path<"$.tires[1].diameter">(bytes);        // 18
cbor::schema<car>::at_path<"$.tires[].diameter">(bytes, 5uz);    // error::index_out_of_bounds
```

## See also

[accessor](accessor.md) · [path](path.md) · [../../advanced/schema.md](../../advanced/schema.md), section 4
