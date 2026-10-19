# cbor::is_singular_query_v

```cpp
template <fixed_string Path>
class is_singular_query;

template <fixed_string Path>
inline constexpr bool is_singular_query_v = is_singular_query<Path>::value;
```

True when `Path` is valid and is a singular query (RFC 9535 2.3.5.1): a
name or an index in each segment, and no descendant segment. A singular
query selects one node at most. An EDN key in brackets is a name.
`is_singular_query<Path>` is a `std::bool_constant` with the same value.

Every form of [at_path](at_path.md) with a path at compile time requires
it. A path that is not singular reads only into a [nodelist](nodelist.md).

## Errors

None. The value is a constant.

## Cost

O(t) at compile time, t the characters of the path. None at run time.

## Example

```cpp
cbor::is_singular_query_v<"$.a[0]['b']">;    // true
cbor::is_singular_query_v<"$.a[*]">;         // false
cbor::is_singular_query_v<"$..a">;           // false
cbor::is_singular_query_v<"$.a[">;           // false: not valid
```

## See also

[is_valid_path_v](is_valid_path_v.md) · [nodelist](nodelist.md) · [../../advanced/path.md](../../advanced/path.md), section 8
