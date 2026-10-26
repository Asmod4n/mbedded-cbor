# lazy

`cbor::lazy` reads a message in place, one step at a time, and copies no byte.

| Function | Does |
|---|---|
| [lazy](lazy.md) | the type: a position in one top-level item |
| [take_ownership](take_ownership.md) | makes a lazy that owns the moved bytes |
| [view](view.md) | makes a lazy that reads bytes in place through their owner |
| [at](at.md) | gives the child at a key or an index |
| [operator[]](operator_subscript.md) | gives the same result as `at` |
| [at_path](at_path.md) | reads one node, or all nodes, at a JSONPath |
| [get](get.md) | reads the value as a C++ type |
| [type](type.md) | gives the kind of the value |
| [size](size.md) | gives the count of an array or a map |
| [empty](empty.md) | tells if the count is 0 |
| [elements](elements.md) | gives the elements of an array as a view |
| [entries](entries.md) | gives the pairs of a map as a view |
| [clone](clone.md) | decodes the item into a tree of `cbor::item` |
| [lazy_elements](lazy_elements.md) | the view of the elements of an array |
| [lazy_entries](lazy_entries.md) | the view of the pairs of a map |
