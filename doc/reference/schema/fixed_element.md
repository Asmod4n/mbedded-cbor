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

```call-seq
element.at_path<"@.member">(indexes...) → std::expected<V, cbor::error>
element.at_path<"$.member">(indexes...) → std::expected<V, cbor::error>
```

```example
bytes: d8 71 82 9a 00 00 00 13 d8 72 84 64 6d 61 6b 65 65 73 65 61 74 73 65 74 69 72 65 73 65 74 72 61 63 6b d8 72 82 68 64 69 61 6d 65 74 65 72 68 70 72 65 73 73 75 72 65 82 19 07 08 82 82 68 64 69 61 6d 65 74 65 72 18 45 82 68 70 72 65 73 73 75 72 65 18 55 5a 00 00 00 0c 00 00 00 71 00 00 00 7b 00 00 00 91 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 f7 7a 00 00 00 05 56 6f 6c 76 6f d9 80 69 82 e2 5a 00 00 00 0c 11 00 00 00 20 40 12 00 00 00 10 40 d8 56 5a 00 00 00 18 00 00 00 00 00 00 f0 3f 00 00 00 00 00 00 00 40 00 00 00 00 00 00 08 40 d9 07 09 d8 80 9a 00 00 00 04 c6 1a 00 00 00 00 18 05 c6 3a 00 00 00 00 c6 1a 00 00 00 01
call: auto const root = cbor::schema<car>::path(std::move(bytes))
result: accessor of a car
call: auto const second = root->at_path<"$.tires[]">(1u)
result: accessor of a tire in a std::vector<tire>
call: second->at_path<"@.diameter">()
result: 18
call: second->at_path<"@.pressure">()
result: 2.25
call: second->at_path<"$.seats">()
result: 5
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
auto const root = cbor::schema<car>::path(std::move(bytes));
auto const second = root->at_path<"$.tires[]">(1uz);
second->at_path<"@.diameter">();   // 18
second->at_path<"@.pressure">();   // 2.25f
second->at_path<"$.seats">();      // 5
```

## See also

[accessor](accessor.md) · [wire](wire.md) · [../../schema.md](../../schema.md), section 4
