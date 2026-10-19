# path

The functions and types that read a value by its JSONPath (RFC 9535).

| Function | Does |
|---|---|
| [at_path](at_path.md) | reads the value at a path, compile time or run time |
| [jsonpath::compile](compile.md) | parses a path at run time into a query |
| [jsonpath::query](query.md) | holds a parsed run-time path |
| [nodelist](nodelist.md) | holds the nodes of a path that selects many |
| [fixed_string](fixed_string.md) | holds a path at compile time |
| [is_valid_path_v](is_valid_path_v.md) | tells if the grammar accepts a path |
| [is_singular_query_v](is_singular_query_v.md) | tells if a path selects one node at most |
