# cbor::nodelist

```cpp
using nodelist = std::vector<lazy>;
```

```call-seq
doc->at_path<"$.a[*]", cbor::nodelist>() → std::expected<nodelist, error>
nodes->size()                            → std::size_t
(*nodes)[i]                              → lazy const &
nodes->empty()                           → bool
```

```example
message: {"a": [1, 2, 3]}
call: auto const nodes = doc->at_path<"$.a[*]", cbor::nodelist>()
result: nodelist of 3 nodes: 1, 2, 3
call: nodes->size()
result: 3
call: (*nodes)[2].get<int>()
result: 3
call: doc->at_path<"$.b", cbor::nodelist>()->empty()
result: true
```

The result of a path that can select many nodes: a wildcard, a
descendant segment, a slice, a filter or several selectors. Each node is a
`cbor::lazy` and holds the owner of the bytes. A nodelist holds the
values, not their places in the item.

`lazy.at_path<Path, cbor::nodelist>()`, `compile<cbor::nodelist>` and
[at_path](at_path.md) with such a query give a nodelist. A path that
selects nothing gives an empty nodelist and no error.

## Errors

The errors of the walk ([at_path](at_path.md)). `nodelist_too_long`: one
step selects more nodes than the top-level item has bytes, which tag 29
makes possible.

## Cost

The walk visits each node that a segment selects. The vector allocates;
it holds N nodes at most, N the bytes of the top-level item.

## Example

```cpp
// bytes: a std::string with {"a": [1, 2, 3]}
auto const doc = cbor::lazy::take_ownership(std::move(bytes)).value();
auto const nodes = doc.at_path<"$.a[*]", cbor::nodelist>();
if (nodes)
    use((*nodes)[2].get<int>());                       // 3, of 3 nodes
doc.at_path<"$.b", cbor::nodelist>()->empty();         // true
```

## See also

[compile](compile.md) · [../lazy/index.md](../lazy/index.md) · [../../advanced/path.md](../../advanced/path.md), section 2
