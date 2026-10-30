# cbor::jsonpath::compile

```cpp
template <class T>
    requires path_result<T>
static std::expected<query<T>, error> compile(std::string_view const path);
```

```call-seq
cbor::jsonpath::compile<T>(path) → std::expected<query<T>, error>
```

```example
message: {"a": [1, 2, 3]}
call: auto const owner = std::make_shared<std::string const>(bytes)
result: shared_ptr to 7 bytes: a1 61 61 83 01 02 03
call: auto const q = cbor::jsonpath::compile<cbor::nodelist>("$.a[?@ > 1]")
result: cbor::jsonpath::query<cbor::nodelist>
call: cbor::at_path(owner, *q)->size()
result: 2
call: cbor::at_path(owner, *cbor::jsonpath::compile<cbor::nodelist>("$.a[0]"))->size()
result: 1
call: cbor::jsonpath::compile<cbor::lazy>("$.a[1]").has_value()
result: true
```

Parses a JSONPath (RFC 9535) at run time and gives a
[query](query.md). The query owns its data, so `path` can end after the
call. An apply of the query does not parse again. `T` is the result type:
a scalar, a view type, `cbor::lazy` or [nodelist](nodelist.md).

The call reads no CBOR. A path at run time reads the same grammar as a
path at compile time, with EDN literals in brackets and in filters.

## Errors

- `invalid_path`: the text is not valid, or it calls `match` or `search`.
- `not_singular_query`: the path selects many nodes and `T` is not
  `nodelist`.
- `nesting_depth_exceeded`: the path is deeper than
  [limits](../errors/limits.md)`::nesting_depth`.

`T` outside `path_result` does not compile.

## Cost

O(t), t the characters of the path. It allocates the parsed path once.

## Example

```cpp
// owner: a std::shared_ptr<std::string const> with {"a": [1, 2, 3]}
auto const q = cbor::jsonpath::compile<cbor::nodelist>("$.a[?@ > 1]");
if (q)
    use(cbor::at_path(owner, *q)->size());                   // 2
cbor::jsonpath::compile<int>("$.a[*]").error();              // not_singular_query
cbor::jsonpath::compile<cbor::lazy>("$.a[").error();         // invalid_path
```

## See also

[query](query.md) · [at_path](at_path.md) · [../../advanced/path.md](../../advanced/path.md), section 9
