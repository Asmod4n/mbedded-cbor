# cbor::fixed_string

```cpp
template <std::size_t N>
struct fixed_string {
    std::array<char, N> value;

    consteval fixed_string(char const (&text)[N]);

    consteval std::string_view view() const;
};
```

The type of a path at compile time. A string literal converts to it, so
`at_path<"$.id", int>` names no `fixed_string`. A program names it in its
own template that passes a path on. `value` holds the characters and the
closing zero. `view()` gives the characters without the zero.

The constructor is `consteval`: a `fixed_string` exists only at compile
time. It checks no grammar; [is_valid_path_v](is_valid_path_v.md) does.

## Errors

None. A text that is not a constant expression does not compile.

## Cost

None at run time.

## Example

```cpp
template <cbor::fixed_string Path>
std::int64_t read_or_zero(std::string_view bytes)
{
    return cbor::at_path<Path, std::int64_t>(bytes).value_or(0);
}
// bytes: {"id": 42}
read_or_zero<"$.id">(bytes);    // 42
read_or_zero<"$.age">(bytes);   // 0
```

## See also

[at_path](at_path.md) · [is_valid_path_v](is_valid_path_v.md) · [is_singular_query_v](is_singular_query_v.md)
