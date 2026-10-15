# Databind

`cbor::structs<T>` writes a C++ value as CBOR and reads it back. A
struct needs no code of its own: the compiler reads its members by
reflection.

Databind needs C++26 reflection: g++ 16 with `-std=c++26 -freflection`.
clang has no reflection yet, so a clang build has no `cbor::structs`.
[basics.md](basics.md) states how errors come back and how long a result
stays valid.

## 1. Write a struct and read it back

```cpp
struct address {
    std::string city;
    std::string zip;
};

struct person {
    std::string name;
    int age;
    std::vector<std::string> tags;
    std::optional<address> home;
};

auto const bytes = cbor::structs<person>::encode(person{"ann", 30, {"admin"}, address{"Graz", "8010"}});
if (!bytes)
    return std::unexpected(bytes.error());
cbor::diagnostic_notation(*bytes);
// {"age": 30, "home": {"zip": "8010", "city": "Graz"}, "name": "ann", "tags": ["admin"]}

auto const back = cbor::structs<person>::decode(*bytes);
if (!back)
    return std::unexpected(back.error());
person const &p = **back;
p.home->city;   // "Graz"
```

`encode` returns a `std::expected<std::string, cbor::error>` with the
bytes. `decode` returns a `std::expected<cbor::owning_ref<person>,
cbor::error>`: `*back` is the `owning_ref`, and `**back` is the
`person`. A struct is a map with the names of its members as keys.
`cbor::diagnostic_notation` prints the bytes as text.

## 2. Types

| C++ type | CBOR |
|---|---|
| `bool` | `false`, `true` |
| an integer type, for example `int` or `std::uint64_t` | an integer |
| `float`, `double` | a float |
| `std::string` | a text |
| `std::vector<std::byte>` | a byte string |
| `std::vector`, `std::list`, `std::set` | an array |
| `std::map`, `std::unordered_map` | a map |
| `std::optional<E>` | the value of E; when empty, `null`, or no key for a member (section 3) |
| an enum with a fixed underlying type | an integer |
| a struct | a map |

An enum needs a fixed underlying type, for example
`enum class color : std::uint8_t { red, green };`. Without it, `decode`
does not compile.

## 3. Keys

The key of a member is its name. A description changes it.
`CBOR_DESCRIBE_STRUCT` stands after the struct, outside every
namespace. It gives the tag of the struct, its generation, and one entry
for each member. The entry `(member, key)` gives the key. A key is a
text or an integer.

```cpp
struct header {
    std::string content_type;
};
CBOR_DESCRIBE_STRUCT(header, (160060, 0), (content_type, "content-type"))
// header{"text/plain"} is 160060({"content-type": "text/plain"})
```

A described struct carries its tag. `decode` gives `incorrect_type`
when the tag is missing or is another tag.

`encode` leaves out a `std::optional` member that is empty. `decode`
gives an empty `std::optional` when its key is not in the map.

## 4. Input from other programs

`decode` reads the keys of a map in any order and skips a key that no
member has. So a program reads the bytes of a newer program that added
members. A new member of type `std::optional` also reads the bytes of an
older program.

| Input | Result |
|---|---|
| a key that no member has | skipped |
| no key for a `std::optional` member | an empty `std::optional` |
| no key for any other member | `key_not_found` |
| a value of another type, for example a text for `int` | `incorrect_type` |
| a number that the type cannot hold | `number_out_of_range` |
| an array of another count for a `std::array` or a `std::tuple` | `incorrect_type` |
| bytes after the value | `syntax_error` |
| the input ends inside the value | `too_little_data` |

```cpp
// bytes: {"age": 30, "name": "ann", "tags": [], "id": 7}
cbor::structs<person>::decode(bytes);   // a person; "id" is skipped, home is empty
// bytes: {"name": "ann"}
cbor::structs<person>::decode(bytes);   // key_not_found: "age" is missing
```

A count in the input reserves no memory, and the limits of
[basics.md](basics.md) hold.

More: the encode into a buffer of your own, views into the bytes, more
types, integer keys, tags, variants, shared values and a memory budget
are in [advanced/structs.md](advanced/structs.md).
