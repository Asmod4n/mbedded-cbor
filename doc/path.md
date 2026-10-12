# Paths

A path reads one value from CBOR bytes, with no decode of the rest. The
syntax is JSONPath (RFC 9535). [basics.md](basics.md) states how errors
come back and how long a result stays valid.

## 1. Read one value

```cpp
// bytes: {"user": {"id": 42, "score": 2.5, "admin": true, "name": "ann"}}
auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
if (id)
    use(*id);                                                // 42
cbor::at_path<"$.user.score", double>(bytes);                // 2.5
cbor::at_path<"$.user.admin", bool>(bytes);                  // true
cbor::at_path<"$.user.age", std::int64_t>(bytes).error();    // key_not_found
```

The path is a string literal, and the compiler checks it: a path that is
not valid does not compile. `bytes` is a `std::string_view`, so a
`std::string` goes in directly. The call reads the bytes in place. It
copies nothing and keeps nothing.

For a number or a truth value, `T` is an integer type, `double` or
`bool`. A `double` also reads an integer that it holds exactly. No other
value converts: a text read as a number gives `incorrect_type`, and `256`
read as `std::uint8_t` gives `number_out_of_range`. `float` and
`std::string` do not compile as `T`.

## 2. Read a text

A text comes back as a view into the bytes. So the call takes an owner of
the bytes, a `std::shared_ptr`, and the result holds it
([basics.md](basics.md), section 2).

```cpp
auto const owner = std::make_shared<std::string const>(bytes);
auto const name = cbor::at_path<"$.user.name", std::string_view>(owner);
if (name)
    use(**name);   // "ann"
```

A byte string reads the same way, as `std::span<std::byte const>`.

## 3. Start at a lazy

A path can start at a `cbor::lazy` ([runtime.md](runtime.md)). `$` is
then the value of the lazy. A text needs no separate owner here, because
the lazy holds the bytes.

```cpp
// bytes: {"cars": [{"hp": 5}, {"hp": 7, "make": "kia"}]}
auto const cars = cbor::lazy::copy(bytes).and_then([](cbor::lazy const &doc) { return doc.at("cars"); });
if (cars) {
    cbor::at_path<"$[1].hp", std::int64_t>(*cars);          // 7
    cbor::at_path<"$[1].make", std::string_view>(*cars);    // "kia"
}
```

## 4. Path syntax

The examples select from this value:

```
{"store": {"book": [{"title": "A", "price": 8},
                    {"title": "B", "price": 12},
                    {"title": "C", "price": 9, "isbn": "x"}],
           "bicycle": {"price": 20}}}
```

| Path | Selects |
|---|---|
| `$` | the whole value |
| `$.store.bicycle.price` | 20, by names |
| `$['store']['bicycle']['price']` | 20, names in brackets, for a name with a blank or a dot |
| `$.store.book[0].title` | "A": an index counts from 0 |
| `$.store.book[-1].title` | "C": a negative index counts from the end |
| `$.store.book[*].price` | 8, 12, 9: every element |
| `$..price` | 8, 12, 9, 20: at any depth |
| `$.store.book[?@.price < 10].title` | "A", "C": a filter |

`at_path` reads a path that selects one value: names and indexes only.
`[*]`, `..`, a filter and a slice such as `[0:2]` select many values, and
only `cbor::query` reads them. `query` gives every match as a
`cbor::lazy`, in a `std::vector`, and an empty vector when nothing matches.

In a filter, `@` is the current value. A filter compares with `==`,
`!=`, `<`, `<=`, `>` and `>=`, and joins tests with `&&`, `||` and `!`.
Numbers compare by value and texts by their bytes. The functions
`length`, `count` and `value` of RFC 9535 work.

An index on a map reads an integer key: `$[1]` reads the key 1 of
`{1: "x"}`. COSE (RFC 9052) and other CBOR protocols use integer keys.

## 5. Errors

The errors of a path are those of [basics.md](basics.md), section 1. A
program tells a missing field from broken input:

```cpp
auto const id = cbor::at_path<"$.user.id", std::int64_t>(bytes);
if (!id) {
    switch (id.error()) {
    case cbor::error::key_not_found:
        use(0);              // no id: a default
        break;
    default:
        reject(id.error());  // the input is not what the program expects
    }
}
```

A path reads the bytes up to its value and no further, so it does not
find a fault after the value. `cbor::item_size(bytes)` checks the whole
value and gives its size in bytes.

## 6. What is not supported

- `match()` and `search()`: the library has no regular expressions.
- Function extensions beyond those of RFC 9535.
- Normalized paths: `query` gives values, not their places.

More: every form of `at_path` and `query`, the full syntax, keys that are
not text, tags, typed arrays, limits and cost are in
[advanced/path.md](advanced/path.md).
