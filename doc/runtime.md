# Run-time API

`cbor::lazy` reads CBOR step by step. It reads only the parts that you
ask for, and a text comes back as a view into the bytes, with no copy.
[basics.md](basics.md) states how errors come back and how long a result
stays valid.

## 1. Read step by step

```cpp
// bytes: {"cars": [{"hp": 5}, {"hp": 7}]}
auto const hp = cbor::lazy::from(bytes)
                    .and_then([](cbor::lazy const &doc) { return doc.at("cars"); })
                    .and_then([](cbor::lazy const &cars) { return cars.at(1); })
                    .and_then([](cbor::lazy const &car) { return car.at("hp"); })
                    .and_then([](cbor::lazy const &h) { return h.get<std::int64_t>(); });
if (hp)
    use(*hp);   // 7
```

`cbor::lazy::from(bytes)` makes a lazy, a position in the bytes. It
copies the bytes once. `cbor::lazy::from(std::move(bytes))` takes a
`std::string` with no copy.

| Step | Goes to |
|---|---|
| `at("name")` | the value of a text key in a map |
| `at(1)` | the element of an array, counted from 0 |
| `at(cbor::key{7})` | the value of an integer key in a map |

Each step returns a `std::expected<cbor::lazy, cbor::error>`. `and_then`
passes the lazy to the next step, and the first error reaches the end of
the chain: `key_not_found`, `index_out_of_bounds`, or `not_indexable`
when a step meets a value that is not a map or an array.

To read one value at a known place, a path is shorter:
`cbor::at_path<"$.cars[1].hp", std::int64_t>(bytes)` ([path.md](path.md)).

The next examples use `->` and `*` on a `std::expected` with no check,
so that they stay short. A program checks each result as above, or uses
`and_then`.

## 2. Read a value as a type

`get<T>()` reads the value at the lazy as `T`.

| T | Reads |
|---|---|
| an integer type, for example `int` or `std::uint64_t` | an integer that fits `T` |
| `double` | a float, or an integer that a `double` holds exactly |
| `bool` | `false` or `true` |
| `std::string_view` | a text, as a `cbor::owning_ref<std::string_view>` |
| `std::span<std::byte const>` | a byte string, as a `cbor::owning_ref<std::span<std::byte const>>` |

Another kind of value gives `incorrect_type`. A number that does not fit
`T` gives `number_out_of_range`.

```cpp
// bytes: {"name": "ann", "age": 300}
auto const doc = cbor::lazy::from(bytes);
auto const name = doc->at("name")->get<std::string_view>();   // **name == "ann"
doc->at("age")->get<std::int64_t>();                          // 300
doc->at("age")->get<std::uint8_t>().error();                  // number_out_of_range
doc->at("name")->get<std::int64_t>().error();                 // incorrect_type
```

## 3. Loop over an array or a map

`elements()` gives the elements of an array, and `entries()` gives the
pairs of a map. Each element is a `std::expected` of its own.

```cpp
// bytes: [1, 2, 3]
std::expected<std::int64_t, cbor::error> sum(std::string_view bytes)
{
    auto const array = cbor::lazy::from(bytes);
    if (!array)
        return std::unexpected(array.error());
    auto const elements = array->elements();
    if (!elements)
        return std::unexpected(elements.error());
    std::int64_t total = 0;
    for (auto const element : *elements) {
        if (!element)
            return std::unexpected(element.error());
        auto const n = element->get<std::int64_t>();
        if (!n)
            return std::unexpected(n.error());
        total += *n;
    }
    return total;   // 6
}
```

A pair of a map has the key in `first` and the value in `second`:

```cpp
// bytes: {"a": 1, "b": 2}
auto const map = cbor::lazy::from(bytes);
for (auto const pair : *map->entries())
    if (pair)
        use(pair->first.get<std::string_view>(), pair->second.get<std::int64_t>());   // "a" 1, then "b" 2
```

## 4. Size and keys

```cpp
// bytes: {"a": 1, "b": 2}
auto const map = cbor::lazy::from(bytes);
map->size();          // 2, the pairs of a map or the elements of an array
map->contains("a");   // true
```

Both return a `std::expected`.

## 5. Print CBOR as text

`cbor::diagnostic_notation(bytes)` gives the CBOR as text, in the
diagnostic notation of RFC 8949. It looks like JSON. Use it to look at
input in a log or in a test.

```cpp
// bytes: the CBOR of section 1
cbor::diagnostic_notation(bytes);   // the text {"cars": [{"hp": 5}, {"hp": 7}]}
```

It returns a `std::expected<std::string, cbor::error>`. Bytes that are
not CBOR give an error.

## 6. Write CBOR

`cbor::structs<T>::encode` writes a C++ value as CBOR
([structs.md](structs.md)). It needs `-freflection`. Without
reflection, `cbor::encode` writes through a binding, a type that you
write; the advanced document describes it.

More: the other forms of `lazy::from`, `find` and `count`, a search
between two iterators, `decode`, `encode` with a binding, CBOR sequences,
`item_size`, shared values and tag 55799 are in
[advanced/runtime.md](advanced/runtime.md).
