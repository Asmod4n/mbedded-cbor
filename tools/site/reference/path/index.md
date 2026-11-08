# path

The functions and types that read a value by its JSONPath (RFC 9535). A path also reads CBOR that JSON does not have: keys that are not text, tags and typed arrays. A path that selects many nodes gives a list.

| Function | Does |
|---|---|
| [at_path](at_path.md) | reads the value at a path, compile time or run time |
| [jsonpath::compile](compile.md) | parses a path at run time into a query |
| [jsonpath::query](query.md) | holds a parsed run-time path |
| [nodelist](nodelist.md) | holds the nodes of a path that selects many |
| [fixed_string](fixed_string.md) | holds a path at compile time |
| [is_valid_path_v](is_valid_path_v.md) | tells if the grammar accepts a path |
| [is_singular_query_v](is_singular_query_v.md) | tells if a path selects one node at most |

## CBOR beyond JSON

An index on a map reads the value under that integer key, as COSE (RFC
9052) uses it. A map has no order in a path, so `$[1]` is the key 1.

```example
bytes: a2 01 61 78 41 01 61 7a
call: doc->at_path<"$[1]">()->get<std::string_view>()
result: ref to "x"
```

A key in EDN notation reads a key that is not text: a byte string, an
array, a tag or a number with a fraction.

```example
bytes: a2 01 61 78 41 01 61 7a
call: doc->at_path<"$[h'01']">()->get<std::string_view>()
result: ref to "z"
```

A path goes into a tag 24 and reads the CBOR item that it holds. It
passes any tag that it does not read, and the value is the content.

```example
bytes: a1 61 61 d8 18 43 82 01 02
call: doc->at_path<"$.a[1]">()->get<int>()
result: 2
```

An index reads one element of a typed array (RFC 8746), here an array of
uint16 in little endian.

```example
bytes: a1 61 76 d8 45 44 07 00 08 00
call: doc->at_path<"$.v[1]">()->get<int>()
result: 8
```
