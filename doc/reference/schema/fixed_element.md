# cbor::schema::accessor<fixed_element<S>>

```cpp
template <class S>
struct fixed_element {};

template <class S>
    requires(has_describe_members<std::remove_cv_t<T>> && tags_registered<T>())
class accessor<fixed_element<S>> {
public:
    template <fixed_string Path, class Self, std::convertible_to<std::size_t>... Index>
        requires(((Path.view().starts_with('@') && reflection::path_valid<S, Path, 1>()) ||
                  (Path.view().starts_with('$') && reflection::path_valid<T, Path, 1>())) &&
                 sizeof...(Index) == reflection::index_arguments<Path>() && std::is_lvalue_reference_v<Self>)
    auto at_path(this Self &&self, Index const... indexes);
};
```

Reads one element of type `S` in a vector of fixed-size structs. Such a
vector is one byte string of its elements under tag 32873
([wire](wire.md)). A path that starts with `@` reads a member of the
element at its offset. A path that starts with `$` starts at the root.

`*it` of the [accessor](accessor.md) of the vector, and `at_path<"@[]">`
on it, give this accessor. It holds the owner of the bytes.

## Errors

- `index_out_of_bounds`: an index past the end of the vector, from the
  step that gave the element.
- A path with `$` gives the errors of [accessor](accessor.md).
- `at_path` on a temporary does not compile. A moved-from accessor
  throws `std::logic_error`.

## Cost

O(p), p the count of steps. No allocation.

## Example

```cpp
// bytes: from schema<car>::encode(car{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}})
auto const root = cbor::packed<car>::path(std::move(bytes));
auto const second = root->at_path<"$.tires[]">(1uz);
second->at_path<"@.diameter">();   // 18
second->at_path<"@.pressure">();   // 2.25f
second->at_path<"$.seats">();      // 5
```

## See also

[accessor](accessor.md) · [wire](wire.md) · [../../schema.md](../../schema.md), section 4
