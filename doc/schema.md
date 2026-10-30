# Packed

A plain CBOR message carries its keys and its structure in every
message. To read one field, a reader walks over the items before it.
`cbor::packed<T>` removes that walk. It writes a struct as Packed CBOR
(draft-ietf-cbor-packed): the struct is the layout, a field of fixed size
sits at an offset that the compiler computes, and a read goes to that
offset directly. FlatBuffers and Cap'n Proto work the same way, but the
message stays CBOR: a decoder of Packed CBOR reads it as a map with the
names of the members as keys.

| | `packed` and `schema` | [`structs`](structs.md) |
|---|---|---|
| Wire form | Packed CBOR, laid out by the struct | any CBOR |
| Read one field | at its offset | walks the items before it |
| Use it for | messages that your programs write and read | CBOR that any program writes, mapped into the members of a struct or class |

`cbor::packed<T>` reads only the description of each struct, so it
builds with C++23 on g++ 15, g++ 16, clang 23 and AppleClang.
`cbor::schema<T>` has the same functions and writes the same bytes. It
exists only with C++26 reflection (g++ 16 with `-std=c++26
-freflection`), and there it checks each description against the
members that reflection reads. This page shows `packed`; each example
works with `schema` too. [basics.md](basics.md) states how errors come
back.

## 1. Write a message and read one field

```cpp
struct sensor {
    std::string name;
    std::uint16_t id;
    std::vector<double> readings;
};

template <>
struct cbor::describe_members<sensor> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1700};
    static constexpr std::tuple members{
        CBOR_MEMBER(sensor, name),
        CBOR_MEMBER(sensor, id),
        CBOR_MEMBER(sensor, readings),
    };
};

auto bytes = cbor::packed<sensor>::encode(sensor{"t1", 7, {1.5, 2.5}});
if (!bytes)
    return std::unexpected(bytes.error());
auto const id = cbor::packed<sensor>::at_path<"$.id">(*bytes);                // 7
auto const second = cbor::packed<sensor>::at_path<"$.readings[1]">(*bytes);   // 2.5
```

`encode` returns a `std::expected<std::string, cbor::error>`, and
`at_path` returns a `std::expected` of the value of the member.

## 2. Tags and keys

`schema` reads each struct from its description, the specialization of
`cbor::describe_members` that `cbor::structs` reads too
([structs.md](structs.md), section 3). The description gives the tag of
the struct and lists every public member with its key. Each struct in a
message has its own tag number.

`CBOR_DEFINE_TYPE(type, tag, members...)` writes the same
specialization in one line, at global scope:

```cpp
struct gauge {
    std::uint16_t level;
};

CBOR_DEFINE_TYPE(gauge, cbor::tag{1710}, CBOR_MEMBER(gauge, level));

cbor::packed<gauge>::encode(gauge{3});
```

A type whose name holds a comma, such as `pair<int, int>`, goes
through an alias, because the preprocessor splits the arguments of a
macro at each comma. clang refuses a description of a type in an
anonymous namespace that no code uses (`-Wunused-const-variable`).

A class with no default constructor is read through a constructor that
takes the described members in the order of the description.

`CBOR_MEMBER(type, member)` takes the name of the member as the key.
`cbor::member{"x", &type::member}` takes the text `x`. A key in `schema`
is a text.

A struct with no description, with `cbor::untagged` or with a base
class, a member that the description leaves out, and two structs with
one number do not compile.

## 3. Read one value

`schema<T>::at_path<Path>(bytes)` reads a number, a `bool`, an enum, or a
`std::optional` of one. A path names members with `.name` and elements
with `[n]`. On a `std::array` or a `std::vector` of `std::uint8_t` or
`std::byte`, `[n]` reads one byte. `[]` takes the index from the next
argument:

```cpp
std::size_t const i = 1;
cbor::packed<sensor>::at_path<"$.readings[]">(*bytes, i);   // 2.5, the element i
```

A path to a member that `T` does not have does not compile. A path to a
text does not compile here: section 4 reads a text.

## 4. Read a text, a list or a struct

`schema<T>::path(std::move(bytes))` opens the message and gives an
accessor. The bytes move into the accessor, with no copy. A
`std::shared_ptr<std::string const>` is read in place. An accessor reads
every kind of member.

```cpp
auto const owner = std::make_shared<std::string const>(std::move(*bytes));
auto const root = cbor::packed<sensor>::path(owner);
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

A list of structs whose members all have a fixed width (numbers, `bool`,
enums, `std::array`s of them other than bytes, and inner structs of the same kind) is one
byte string of its elements: little endian, no padding, under tag 32873.
The table at the start of the message describes the element once: its
class tag and the key and RFC 8746 type of each member. The accessor of
such a list is a range of element accessors, so a page is
`std::views::drop` and `std::views::take`:

```cpp
struct point {
    float x;
    float y;
};

struct track {
    std::vector<point> points;
};

template <>
struct cbor::describe_members<point> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1703};
    static constexpr std::tuple members{CBOR_MEMBER(point, x), CBOR_MEMBER(point, y)};
};

template <>
struct cbor::describe_members<track> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1704};
    static constexpr std::tuple members{CBOR_MEMBER(track, points)};
};

auto message = cbor::packed<track>::encode(track{{{1, 2}, {3, 4}, {5, 6}}});
auto const lane = cbor::packed<track>::path(std::move(*message));
auto const points = lane->at_path<"$.points">();
for (auto const p : *points | std::views::drop(1) | std::views::take(1))
    use(*p.at_path<"@.y">());                   // 4
lane->at_path<"$.points[2].x">();               // 5
```

A list of structs with a text, a list or another part of variable size
inside stays a list of records, each under the tag of its class.

The accessor holds the bytes. A `std::string_view` from it stays valid
while the accessor lives. Keep the accessor in a variable: `at_path` on a
temporary accessor does not compile.

## 5. Read the whole struct

```cpp
auto const back = cbor::packed<sensor>::decode(owner);
if (back)
    use((*back)->readings.size());   // 2
```

`decode` returns a `std::expected<cbor::ref<sensor>, cbor::error>`,
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

## 7. Read from a peer that you trust

`unverified_path`, `unverified_at_path` and `unverified_decode` take the
same arguments as `path`, `at_path` and `decode`. They do not check the
tags and the heads in the message. They check every bound, so a read
never goes outside the message. A wrong byte gives a wrong value, not an
error.

```cpp
auto const fast = cbor::packed<sensor>::unverified_path(owner);
```

[reference/schema/unverified.md](reference/schema/unverified.md) lists
what each form checks.

More: the bytes of a message, every check, the accessor in detail, the
typed array view, a memory budget and the cost of each call are in
[advanced/schema.md](advanced/schema.md).
