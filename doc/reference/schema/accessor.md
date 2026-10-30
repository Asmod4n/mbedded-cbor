# cbor::schema::accessor

```cpp
template <class U = T>
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
class accessor {
public:
    template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
        requires(((Path.view().starts_with('@') && reflection::path_valid<U, Path, 1>()) ||
                  (Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>())) &&
                 sizeof...(Index) == reflection::index_arguments<Path>() && std::is_lvalue_reference_v<Self>)
    auto at_path(this Self &&self, Index const... indexes);

    template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
        requires(std::same_as<U, T> && Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>() &&
                 sizeof...(Index) == reflection::index_arguments<Path>())
    auto view(this Self &&self, Index const... indexes);

    std::size_t size() const requires(listed);

    class iterator;   // value_type is accessor<fixed_element<element>>
    iterator begin() const requires(reflection::is_fixed_vector<U>);
    iterator end() const requires(reflection::is_fixed_vector<U>);
};
```

```call-seq
accessor.at_path<"@.member">(indexes...) → std::expected<V, cbor::error>
accessor.at_path<"$.member">(indexes...) → std::expected<V, cbor::error>
root.view<"$.list">(indexes...)          → std::expected<cbor::typed_array_view<E>, cbor::error>
accessor.size()                          → std::size_t
accessor.begin()                         → iterator
accessor.end()                           → iterator
```

```example
bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: auto const root = cbor::schema<car>::path(std::move(bytes))
result: accessor of a car
call: auto const tires = root->at_path<"$.tires">()
result: accessor of a std::vector<tire> of 2 elements
call: tires->size()
result: 2
call: tires->at_path<"@[1].diameter">()
result: 18
call: tires->at_path<"$.seats">()
result: 5
call: auto const t = *tires->begin()
result: accessor of a tire in a std::vector<tire>
call: t.at_path<"@.diameter">()
result: 17
call: auto const u = tires->begin()[1]
result: accessor of a tire in a std::vector<tire>
call: u.at_path<"@.diameter">()
result: 18
call: root->view<"$.track">()->size()
result: 3
call: tires->at_path<"@[]">(2u)
result: unexpected cbor::error::index_out_of_bounds
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
