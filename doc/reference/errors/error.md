# cbor::error

```cpp
enum class error {
    too_little_data = 1,
    syntax_error,
    indefinite_length,
    nesting_depth_exceeded,
    inadmissible_type_for_tag_content,
    sharedref_index_not_marked,
    sharedref_index_out_of_range,
    sharedref_not_complete,
    reserved_simple_value,
    unsupported_value,
    not_indexable,
    index_out_of_bounds,
    key_not_found,
    invalid_path,
    incorrect_type,
    number_out_of_range,
    cyclic_data_structure,
    unpopulated_table_index,
    nodelist_too_long,
    duplicate_key,
    no_buffer_space,
    value_too_large,
    not_enough_memory,
    io_error,
    string_length_exceeded,
    container_elements_exceeded,
    not_singular_query
};
```

The fault that a call returns in its `std::expected<T, cbor::error>`.
Bad input never throws. The first value is 1. So a `std::error_code`
from a `cbor::error` is always true as a `bool`. A wrong use of the library is not an
error value: it does not compile, or it throws `std::logic_error`.

| Error | When |
|---|---|
| `too_little_data` | The bytes end before the item ends. |
| `syntax_error` | A head is not well-formed, or a simple value is reserved in the two-byte form. Bytes after the item, where a call reads one item. |
| `indefinite_length` | A head uses indefinite length, and the call does not read it. |
| `nesting_depth_exceeded` | The item or the path is deeper than [limits](limits.md)`::nesting_depth`. |
| `inadmissible_type_for_tag_content` | A tag has content that its definition does not allow. |
| `sharedref_index_not_marked` | A tag 29 names no tag 28 before it. |
| `sharedref_index_out_of_range` | A tag 29 has an index that does not fit `std::size_t`. |
| `sharedref_not_complete` | `structs` decode meets a tag 29 that names a tag 28 in a skipped member. |
| `reserved_simple_value` | An encode meets a simple value from 24 to 31. |
| `unsupported_value` | The encoder meets a typed array with a reserved tag or a tag outside RFC 8746. `structs` meets a map key that is an array, a map or a tag. |
| `not_indexable` | A key or an index meets an item that is not a map or an array. |
| `index_out_of_bounds` | The index is outside the array. |
| `key_not_found` | The key is not in the map. |
| `invalid_path` | The path text is not valid. |
| `incorrect_type` | A typed read finds another kind of item than T takes. |
| `number_out_of_range` | The number does not fit T. |
| `cyclic_data_structure` | The library does not return it. A writer of the program can return it for a cycle. |
| `unpopulated_table_index` | A reference in a `schema<T>` message names an entry that its table does not have. |
| `nodelist_too_long` | One step of a path selects more nodes than the item has bytes. |
| `duplicate_key` | A deterministic check finds a key twice in one map. |
| `no_buffer_space`, `value_too_large`, `not_enough_memory`, `io_error` | The writer of an encode gives this error. `not_enough_memory` also comes from a refused allocation in `structs` and `schema` decode. |
| `string_length_exceeded` | A string is longer than [limits](limits.md)`::string_length`. |
| `container_elements_exceeded` | A container has more items than [limits](limits.md)`::container_elements`. |
| `not_singular_query` | `jsonpath::compile<T>` gets a path that is not singular, and `T` is not `nodelist`. |

## Errors

Not applicable: the type is the error.

## Cost

O(1). An error is one `int`. It does not allocate.

## Example

```cpp
// bytes: {"user": {"id": 42}}
auto const age = cbor::at_path<"$.user.age", std::int64_t>(bytes);
age.error();                                // key_not_found
std::error_code{age.error()}.message();     // "key not found"
```

## See also

[make_error_code](make_error_code.md) · [error_category](error_category.md) · [../../basics.md](../../basics.md), section 1 · [../../advanced/basics.md](../../advanced/basics.md), section 3
