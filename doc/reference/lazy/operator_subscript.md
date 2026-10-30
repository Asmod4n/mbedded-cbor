# cbor::lazy::operator[]

```cpp
std::expected<lazy, error> operator[](std::string_view key) const;
std::expected<lazy, error> operator[](key k) const;
std::expected<lazy, error> operator[](std::size_t index) const;
```

```call-seq
(*doc)["key"]         → std::expected<lazy, error>
(*doc)[cbor::key{7}]   → std::expected<lazy, error>
(*doc)[1]              → std::expected<lazy, error>
```

```example
message: {"cars": [5, 7]}
call: (*(*doc)["cars"])[1]->get<int>()
result: 7
call: (*(*doc)["cars"])[0]->get<int>()
result: 5
```

Gives the same result as [at](at.md) with the same argument. It does not
throw for a missing key or a wrong index. The result is a
`std::expected`, so `l["a"]["b"]` does not compile. A path reads a chain
([at_path](at_path.md)).

## Errors

The errors of [at](at.md).

## Cost

The cost of [at](at.md).

## Example

```cpp
// bytes: a std::string with {"cars": [5, 7]}
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
(*(*doc)["cars"])[1]->get<int>();   // 7
(*doc)["bus"].error();              // key_not_found
```

## See also

[at](at.md) · [at_path](at_path.md)
