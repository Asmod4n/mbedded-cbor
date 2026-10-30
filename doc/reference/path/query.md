# cbor::jsonpath::query

```cpp
template <class T>
    requires path_result<T>
class query
{
public:
    using value_type = T;
    using result_type = std::conditional_t<view_result<T>, ref<T>, T>;

    query(query const &) = default;
    query &operator=(query const &) = default;
    query(query &&other) noexcept;
    query &operator=(query &&other) noexcept;
    ~query() = default;
};
```

```call-seq
cbor::jsonpath::compile<std::int64_t>("$.id").value() → query<std::int64_t>
cbor::at_path(bytes, q)                               → std::expected<std::int64_t, error>
```

```example
message: {"id": 1}
call: auto const q = cbor::jsonpath::compile<std::int64_t>("$.id").value()
result: cbor::jsonpath::query<long long>
call: cbor::at_path(bytes, q)
result: 1
```

```example
message: {"id": 2, "x": 0}
call: auto const q = cbor::jsonpath::compile<std::int64_t>("$.id").value()
result: cbor::jsonpath::query<long long>
call: cbor::at_path(bytes, q)
result: 2
```

A parsed run-time path with the result type `T`. Only
[compile](compile.md) makes one: the default constructor is private.
The query holds its own copy of the path data, and no view into the text
of the path. One query reads any number of items.

[at_path](at_path.md) and `lazy::at_path(e)` apply it. `result_type` is
what an apply gives: `ref<T>` for a view type, `T` for all other types.

## Errors

The query itself gives no error. An apply of a moved-from query throws
`std::logic_error`.

## Cost

A copy copies the parsed path and allocates. A move allocates nothing.

## Example

```cpp
// first: {"id": 1}, second: {"id": 2, "x": 0}
auto const q = cbor::jsonpath::compile<std::int64_t>("$.id").value();
use(cbor::at_path(first, q));    // 1
use(cbor::at_path(second, q));   // 2
```

## See also

[compile](compile.md) · [at_path](at_path.md) · [nodelist](nodelist.md)
