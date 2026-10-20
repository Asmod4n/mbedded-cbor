# cbor::result

```cpp
template <class T>
class result : public std::expected<T, error>;
```

Every function of the library that can fail returns a `result<T>`. It
is a `std::expected<T, cbor::error>` and converts to one. It differs in
two points:

- `*r` and `r.value()` on a temporary give the value itself, not a
  reference into the temporary. A range-for over the value of a
  temporary result therefore reads a living value with every C++23
  compiler. Before C++23 P2718 (GCC 15, clang 19), `std::expected`
  destroys the temporary before the loop starts.
- `and_then`, `or_else`, `transform` and `transform_error` return a
  `result` again. The callable of `and_then` and `or_else` may return a
  `result` or a `std::expected`.

On a named result, `*r` and `r.value()` give references, as on
`std::expected`. A chain that starts from a `std::expected` takes the
rules of `std::expected`: its `and_then` needs a callable that returns a
`std::expected`.

## Errors

`value()` throws `std::bad_expected_access<cbor::error>` when the result
holds an error, as `std::expected` does.

## Cost

`*r` and `value()` on a temporary move the value out once. No
allocation.

## Example

```cpp
// doc: a lazy of {"k": [1, 2, 3]}
std::int64_t sum = 0;
for (auto const e : *doc.at("k").and_then([](cbor::lazy const &k) { return k.elements(); }))
    sum += e->get<std::int64_t>().value_or(0);              // 6

std::expected<cbor::lazy, cbor::error> const plain = doc.at("k");   // converts
```

## See also

[error](error.md) · [../lazy/elements.md](../lazy/elements.md)
