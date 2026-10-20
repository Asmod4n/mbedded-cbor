# cbor::structs<T>::encode

```cpp
static cbor::result<std::string> encode(T const &value);

template <class Target>
static cbor::result<std::size_t> encode(T const &value, Target &&target);
```

Writes `value` as one CBOR item in preferred serialization. A struct is
a map, and its keys come in the order of RFC 8949 4.2.1. The first form
returns the bytes in a new `std::string`.

The second form appends to `target` and returns the count of bytes that
it appended. `target` is a `std::string`, a container of `char`, a
`std::span<char>` or a writer. The function writes no shared value
(tag 28, tag 29).

## Errors

- `no_buffer_space`: a `std::span<char>` that is too small for the bytes.
- `value_too_large`: the size of the bytes does not fit in `std::size_t`.
- An error of the writer: `no_buffer_space`, `value_too_large`,
  `not_enough_memory` or `io_error`.
- A `std::chrono::year_month_day` that is no valid date throws
  `std::logic_error`.
- A refused allocation of the result throws `std::bad_alloc`.
- A type that `structs` does not take does not compile ([types](types.md)).

## Cost

O(i + n): i items, n bytes. The first form allocates one `std::string`.
A container target grows by O(n). A span that holds the bytes allocates
nothing. A span that is too small and a writer use a temporary of O(n).

## Example

```cpp
struct person {
    std::string name;
    int age;
};
auto const bytes = cbor::structs<person>::encode(person{"ann", 30});
if (bytes)
    use(*bytes);   // {"age": 30, "name": "ann"}
```

## See also

[decode](decode.md) · [types](types.md) · [../../advanced/structs.md](../../advanced/structs.md), section 1
