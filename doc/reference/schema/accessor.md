# cbor::schema::accessor

```cpp
template <class U = T>
    requires(std::is_class_v<T> && std::is_aggregate_v<T> && tags_registered<T>())
class accessor {
public:
    template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
        requires(((Path.view().starts_with('@') && reflection::path_valid<U, Path, 1>()) ||
                  (Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>())) &&
                 sizeof...(Index) == /* the count of [] in Path */ && std::is_lvalue_reference_v<Self>)
    auto at_path(this Self &&self, Index const... indexes);

    template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
        requires(std::same_as<U, T> && Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>() &&
                 sizeof...(Index) == /* the count of [] in Path */)
    auto view(this Self &&self, Index const... indexes);

    std::size_t size() const requires(listed);

    class iterator;   // value_type is accessor<fixed_element<element>>
    iterator begin() const requires(reflection::is_fixed_vector<U>);
    iterator end() const requires(reflection::is_fixed_vector<U>);
};
```

Reads the part `U` of a message of `T`. A path that starts with `@`
starts at `U`. A path that starts with `$` starts at the root. The
result is a `std::expected`: the value of a number, `bool` or enum; a
`std::string_view` of a text; the accessor of a struct, a list or a map.
Every result holds the owner of the bytes.

`view` gives a [typed_array_view](typed_array_view.md) of a list of
numbers. It exists on the root accessor only. `size()` gives the count
of a list or a map. An accessor of a vector of fixed-size structs is a
random access range of [fixed_element](fixed_element.md) accessors.

## Errors

- `index_out_of_bounds`: an index past the end of a list.
- `incorrect_type`: a wrong head or tag on the path.
- `unpopulated_table_index`, `syntax_error`: a broken reference.
- `at_path` on a temporary does not compile. A moved-from accessor
  throws `std::logic_error`.

## Cost

`at_path`, `view`: O(p), p the count of steps. `size`, `begin`, `end`:
O(1). No allocation.

## Example

```cpp
// bytes: from schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}})
auto const root = cbor::schema<car>::path(std::move(bytes));
auto const tires = root->at_path<"$.tires">();
tires->size();                       // 2
tires->at_path<"@[1].diameter">();   // 18
tires->at_path<"$.seats">();         // 5
for (auto const t : *tires)
    use(*t.at_path<"@.diameter">()); // 17, then 18
root->view<"$.track">()->size();     // 3
```

## See also

[path](path.md) · [fixed_element](fixed_element.md) · [typed_array_view](typed_array_view.md) · [../../advanced/schema.md](../../advanced/schema.md), section 5
