# Schema

`cbor::packed<T>` writes a struct as one message and reads one field of
the message with no decode of the rest. A field of fixed size sits at an
offset that the compiler computes, so a read goes to the field and does
not read the fields before it.

Use `schema` for messages that your programs write and read one field at
a time. Use [structs.md](structs.md) for plain CBOR maps that any
program writes. A decoder of Packed CBOR (draft-ietf-cbor-packed) reads a
schema message as a map with the names of the members as keys.

Schema needs C++26 reflection: g++ 16 with `-std=c++26 -freflection`.
clang has no reflection yet, so a clang build has no `cbor::packed`.
[basics.md](basics.md) states how errors come back.

## 1. Write a message and read one field

```cpp
struct [[=cbor::tag(1700)]] sensor {
    std::string name;
    std::uint16_t id;
    std::vector<double> readings;
};

auto const bytes = cbor::packed<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
if (!bytes)
    return std::unexpected(bytes.error());
auto const id = cbor::packed<sensor>::at_path<"$.id">(*bytes);                // 7
auto const second = cbor::packed<sensor>::at_path<"$.readings[1]">(*bytes);   // 2.5
```

`encode` returns a `std::expected<std::string, cbor::error>`, and
`at_path` returns a `std::expected` of the value of the member.

## 2. Tags

Every struct in a message carries `[[=cbor::tag(n)]]`, and each struct
has its own number. A struct with no tag, or two structs with one
number, do not compile.

The key of a member is its name. `[[=cbor::key("x")]]` changes it, as in
structs. Only public members are written.

## 3. Read one value

`packed<T>::at_path<Path>(bytes)` reads a number, a `bool`, an enum, or a
`std::optional` of one. A path names members with `.name` and elements
with `[n]`. `[]` takes the index from the next argument:

```cpp
std::size_t const i = 1;
cbor::packed<sensor>::at_path<"$.readings[]">(*bytes, i);   // 2.5, the element i
```

A path to a member that `T` does not have does not compile. A path to a
text does not compile here: section 4 reads a text.

## 4. Read a text, a list or a struct

`packed<T>::path(bytes)` opens the message and gives an accessor. An
accessor reads every kind of member.

```cpp
auto const root = cbor::packed<sensor>::path(*bytes);
if (!root)
    return std::unexpected(root.error());
root->at_path<"$.name">();                      // "t1", a std::string_view
auto const readings = root->view<"$.readings">();
if (readings)
    for (double const r : *readings)
        use(r);                                 // 1.5, then 2.5
```

| The path ends at | `at_path` gives, in a `std::expected` |
|---|---|
| a number, a `bool` or an enum | the value |
| a text or a byte string | a `std::string_view` into the message |
| a struct, a list or a map | the accessor of that part |
| a `std::optional<E>` | a `std::optional` of the result of E |

`view<Path>()` gives a list of numbers as a range with `size()` and
`at(i)`.

The accessor holds the bytes. A `std::string_view` from it stays valid
while the accessor lives. Keep the accessor in a variable: `at_path` on a
temporary accessor does not compile.

## 5. Read the whole struct

```cpp
auto const back = cbor::packed<sensor>::decode(*bytes);
if (back)
    use((*back)->readings.size());   // 2
```

`decode` returns a `std::expected<cbor::owning_ref<sensor>, cbor::error>`,
as `structs` does.

## 6. Errors

| Input | Error |
|---|---|
| a message that ends too early | `too_little_data` |
| a message of another struct | `incorrect_type` |
| an index past the end of a list | `index_out_of_bounds` |
| a refused allocation in `decode` | `not_enough_memory` |

Every read checks the bytes that it reads. A read never goes outside the
message, and a field with a wrong head gives an error.

More: the bytes of a message, every check, the accessor in detail, the
typed array view, a memory budget and the cost of each call are in
[advanced/packed.md](advanced/packed.md).
