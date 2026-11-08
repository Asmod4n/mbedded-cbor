# Run-time API

`cbor::lazy` reads CBOR step by step. It reads only the parts that you
ask for, and a text comes back as a view into the bytes, with no copy.
[basics.md](basics.md) states how errors come back and how long a result
stays valid. The reference with examples that run in the browser:
<https://asmod4n.github.io/mbedded-cbor/lazy.html> and
<https://asmod4n.github.io/mbedded-cbor/item.html>.

## 1. Read step by step

```cpp
// bytes: {"cars": [{"hp": 5}, {"hp": 7}]}
auto const hp = cbor::lazy::take_ownership(std::move(bytes))
                    .and_then([](cbor::lazy const &doc) { return doc.at("cars"); })
                    .and_then([](cbor::lazy const &cars) { return cars.at(1); })
                    .and_then([](cbor::lazy const &car) { return car.at("hp"); })
                    .and_then([](cbor::lazy const &h) { return h.get<std::int64_t>(); });
if (hp)
    use(*hp);   // 7
```

`cbor::lazy::take_ownership(std::move(bytes))` makes a lazy, a position
in the bytes. Bytes go into the library in three forms, and no form
copies them:

| Form | The bytes |
|---|---|
| `lazy::take_ownership(std::move(bytes))` | `bytes` is a `std::string`; it moves into storage that the lazy owns |
| `lazy::view(owner)` | `owner` is a `std::shared_ptr<std::string const>`; no copy, and every result keeps `owner` alive |
| `lazy::view(owner, part)` | `part` is a `std::string_view` into memory that `owner`, a `std::shared_ptr<void const>`, keeps alive |

```cpp
auto const owner = std::make_shared<std::string const>(std::move(bytes));
auto const doc = cbor::lazy::view(owner);   // reads *owner in place
```

| Step | Goes to |
|---|---|
| `at("name")` | the value of a text key in a map |
| `at(1)` | the element of an array, counted from 0 |
| `at(cbor::key{7})` | the value of an integer key in a map |
| `["name"]`, `[1]`, `[cbor::key{7}]` | the same as `at` |

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
| `std::string_view` | a text, as a `cbor::ref<std::string_view>` |
| `std::span<std::byte const>` | a byte string, as a `cbor::ref<std::span<std::byte const>>` |

A `cbor::ref` views the bytes and keeps their owner alive. Bind it to a
variable before you read it: `*` and `->` of a temporary `cbor::ref` do
not compile.

Another kind of value gives `incorrect_type`. A number that does not fit
`T` gives `number_out_of_range`.

```cpp
// bytes: {"name": "ann", "age": 300}
auto const doc = cbor::lazy::take_ownership(std::move(bytes));
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
std::expected<std::int64_t, cbor::error> sum(std::string bytes)
{
    auto const array = cbor::lazy::take_ownership(std::move(bytes));
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
auto const map = cbor::lazy::take_ownership(std::move(bytes));
for (auto const pair : *map->entries())
    if (pair)
        use(pair->first.get<std::string_view>(), pair->second.get<std::int64_t>());   // "a" 1, then "b" 2
```

## 4. Size and keys

```cpp
// bytes: {"a": 1, "b": 2}
auto const map = cbor::lazy::take_ownership(std::move(bytes));
map->size();          // 2, the pairs of a map or the elements of an array
map->at("z").error(); // key_not_found, the key is not in the map
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

`cbor::structs<T>::encode` writes a C++ value as CBOR, on every
compiler ([structs.md](structs.md)). `cbor::encoder` writes one item
per call:

```cpp
std::string out;
cbor::encoder e(out);
e.map(1);
e.text_string("a");
e.unsigned_integer(std::uint8_t{5});
e.finish();   // out holds a1 61 61 18 05, {"a": 5_0}
```

An integer takes the width of its type, so `std::uint8_t{5}` takes a head of
two bytes.

## 7. Read items from a socket

A socket or a pipe gives bytes in pieces. A piece can end in the
middle of an item. `cbor::sequence_reader` reads a CBOR sequence
(RFC 8742): items one after the other, with nothing between them.

```cpp
cbor::sequence_reader r;
r.feed(std::move(chunk));         // a std::string that arrived
while (auto item = r.next())      // each item that is complete
    handle(*item);                // a cbor::lazy
// r.next().error() == cbor::error::too_little_data: wait for more bytes
```

`feed` takes a `std::string` that you move in without a copy, and a
`std::shared_ptr<std::string const>` without a copy. It copies a
`std::string_view` and a `std::string` that you do not move. `next`
gives one item when all of its bytes are there. It gives
`too_little_data` while it waits. The reader continues where it
stopped.

Each item is a `cbor::lazy` that keeps its bytes alive. You can keep
it after later calls of `feed` and after the reader is gone. The
reader copies only an item that a piece cuts.

Bytes that are not CBOR give their error, for example
`syntax_error`. After such an error every call gives the same error,
and `feed` keeps no byte, because RFC 8742 gives no way to find the
start of the next item.

At the end of the stream, `r.empty()` tells you whether the reader
still holds bytes of an item that is not complete.

The reader refuses an item nested deeper than `cbor::limits` allows.
The other limits apply when you read the item. Bound the bytes that
you give to the reader.

More: `operator[]`, a map with a key twice, `clone` and `decode`, the encoder, CBOR sequences,
`item_size`, shared values and tag 55799 are in
[advanced/runtime.md](advanced/runtime.md).
