# cbor::structs<T>::encode

```cpp
static std::expected<std::string, cbor::error> encode(T const &value);

template <class Target>
static std::expected<std::size_t, cbor::error> encode(T const &value, Target &&target);
```

```call-seq
cbor::structs<T>::encode(value)         → std::expected<std::string, cbor::error>
cbor::structs<T>::encode(value, target) → std::expected<std::size_t, cbor::error>
```

```example
message: {"name": "ann", "age": 30}
call: auto const encoded = cbor::structs<person>::encode(person{"ann", 30})
result: 16 bytes: a2 63 61 67 65 18 1e 64 6e 61 6d 65 63 61 6e 6e
call: cbor::lazy::take_ownership(*encoded)
result: lazy of {"age": 30, "name": "ann"}
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
