# cbor::is_valid_path_v

```cpp
template <fixed_string Path>
class is_valid_path;

template <fixed_string Path>
inline constexpr bool is_valid_path_v = is_valid_path<Path>::value;
```

```call-seq
cbor::is_valid_path_v<"$..a">                                 → bool
cbor::jsonpath::compile<cbor::nodelist>("$..a").has_value()   → bool
```

```example
message: {"a": 1}
call: cbor::jsonpath::compile<cbor::nodelist>("$..a").has_value()
result: true
call: cbor::jsonpath::compile<cbor::nodelist>("$.a[").has_value()
result: false
call: cbor::jsonpath::compile<cbor::nodelist>("$[?match(@.t, 'a.c')]").has_value()
result: false
```

True when the grammar of RFC 9535, with EDN literals, accepts `Path`, and
the path is not deeper than [limits](../errors/limits.md)`::nesting_depth`.
`is_valid_path<Path>` is a `std::bool_constant` with the same value.

A path with `match` or `search` is not valid here. `lazy.at_path<Path>()`
and `lazy.at_path<Path, cbor::nodelist>()` require a valid path.

## Errors

None. The value is a constant.

## Cost

O(t) at compile time, t the characters of the path. None at run time.

## Example

```cpp
cbor::is_valid_path_v<"$..a">;                     // true
cbor::is_valid_path_v<"$.a[">;                     // false
cbor::is_valid_path_v<"$[?match(@.t, 'a.c')]">;    // false
```

## See also

[is_singular_query_v](is_singular_query_v.md) · [compile](compile.md) · [../../advanced/path.md](../../advanced/path.md), section 8
