# Databind in detail

This page adds the details to [../structs.md](../structs.md). A
comment shows the CBOR as text.

## 1. Encode into a buffer of your own

| Call | Result |
|---|---|
| `structs<T>::encode(value)` | `std::expected<std::string, error>`, the bytes |
| `structs<T>::encode(value, target)` | `std::expected<std::size_t, error>`, the count of bytes appended to `target` |

`encode` with a target appends. The target is a `std::string`, a
container of `char`, a `std::span<char>` or a writer
([runtime.md](runtime.md)).

```cpp
std::string out;
cbor::structs<std::uint8_t>::encode(1, out);   // 1
cbor::structs<std::uint8_t>::encode(2, out);   // 1; out holds the bytes of 1 and of 2
```

## 2. Views into the bytes and their owner

A result can hold views into the bytes: `std::string_view` and
`std::span<std::byte const>`. So `decode` always returns an
`owning_ref<T>`, which holds the owner of the bytes. A view in the result
stays valid while the `owning_ref` lives. A result with no view member
holds its own copy of every value.

| Call | Owner |
|---|---|
| `decode(std::string)` with an rvalue | the library moves the string into a new `std::shared_ptr<std::string const>` |
| `decode(std::string_view)` | the library copies the bytes into a new `std::shared_ptr<std::string const>` |
| `decode(std::shared_ptr<std::string const>)` | the caller, no copy |
| `decode(std::shared_ptr<std::string const>, target)` | the caller, and `decode` reads into `target` (section 8) |

A `std::string` lvalue and a `char const *` take the copying form.

```cpp
// bytes: "abc"
cbor::structs<std::string_view>::decode(bytes);                 // a copy of bytes
auto const owner = std::make_shared<std::string const>(bytes);
cbor::structs<std::string_view>::decode(owner);         // a view into *owner
cbor::structs<std::string_view>::decode(std::move(bytes));      // no copy
```

An empty owner throws `std::logic_error`. A `std::string` or a
`std::string_view` beside an owner does not compile
([basics.md](basics.md), section 8).

## 3. Every type

| C++ type | CBOR item |
|---|---|
| `bool` | `true`, `false` |
| `std::nullptr_t` | `null` |
| `cbor::simple_value` | a simple value |
| an integer of 8 to 64 bits | an integer |
| `__int128`, `unsigned __int128` | an integer, or a bignum (tag 2 or 3) |
| an enum with a fixed underlying type | the integer of its underlying type |
| `float`, `double` | the shortest float that holds the value exactly |
| `std::string`, `std::pmr::string`, `std::string_view` | a text string |
| `std::vector<std::byte>`, `std::array<std::byte, N>`, `std::span<std::byte const>` | a byte string |
| `std::optional<E>` | the item of E, or `null` when empty |
| `std::variant<E...>` | the item of the alternative it holds |
| `std::tuple<E...>`, `std::array<E, N>` | an array of exactly that count |
| `std::map`, `std::unordered_map` and their `std::pmr` forms | a map |
| `std::vector`, `std::deque`, `std::list`, `std::set` and other containers | an array |
| `cbor::tagged<N, E>` | tag N on the item of E |
| a struct | a map, section 4 |

```cpp
cbor::structs<std::set<std::uint8_t>>::encode({3, 1});                          // [1, 3]
cbor::structs<std::array<std::byte, 2>>::encode({std::byte{1}, std::byte{2}});  // h'0102'
cbor::structs<cbor::tagged<1, std::int64_t>>::encode({1700000000});             // 1(1700000000)
```

An enum with no fixed underlying type does not compile in `decode`,
because some values of its underlying type are not values of the enum.

A `float` takes a binary16, binary32 or binary64 item. A binary64 item
that a `float` cannot hold exactly gives `error::number_out_of_range`.

## 4. Structs and keys

A struct is a map. Each public data member is one entry. The keys
come in the order of RFC 8949 4.2.1: sorted by their encoded bytes.

An aggregate with no description is read by the names of its members:
through reflection on a build with `-freflection`, through Boost.PFR on
other builds. It takes no annotation, so it gives the same bytes on
every build. An aggregate with a base class needs a description.

`CBOR_DESCRIBE_STRUCT(type, (tag, generation), entries...)` describes a
struct or a class:

| Entry | Key |
|---|---|
| `(m)` | the text of the name `m` |
| `(m, "x")` | the text `x` |
| `(m, 4)`, `(m, -1)` | the integer |
| `static constexpr std::array<std::int64_t, N> keys` in an aggregate with no description | the keys are these integers, one for each member |

- The macro stands outside every namespace, after the type and after the
  description of each struct that the type holds.
- A type whose name holds a comma takes a type alias.
- An entry names a public data member of the type or of a public base.
- On a build with `-freflection`, the entries are exactly the public data
  members of the type and of its public bases. Another list does not
  compile.
- The macro takes at most `cbor::describe_entries_max` (342) entries.
- No C++ literal holds -2^63. The key -2^63 is
  `std::numeric_limits<std::int64_t>::min()`.
- A described class needs a default constructor for `decode(bytes)`.
  A class with no default constructor decodes into an object that the
  caller gives: `decode(owner, T(...))`.

```cpp
class account {
public:
    account() = default;
    std::string user;
    std::uint8_t level = 0;
private:
    std::string password;
};
CBOR_DESCRIBE_STRUCT(account, (160061, 0), (user), (level, 1))
// an account with user "ann" and level 2 is 160061({1: 2, "user": "ann"})

struct senml {          // RFC 8428 6: n is 0, v is 2
    std::string n;
    double v;
    static constexpr std::array<std::int64_t, 2> keys{0, 2};
};
// senml{"t", 1.5} is {0: "t", 2: 1.5}
```

`decode` reads the entries of a map in any order:

- a key that no member has: the entry is skipped;
- a key that comes twice: the first entry gives the value;
- a missing key of a member that is not optional: `error::key_not_found`;
- a key that is an array, a map or a tag: `error::unsupported_value`.

## 5. Tags

A described struct carries the tag of its description (section 4).
`cbor::tagged<N, E>` writes tag N on the item of E. A struct that
carries `[[=cbor::tag(n)]]` does not compile in `structs`.

```cpp
struct person { std::string name; std::int64_t age; };
cbor::structs<cbor::tagged<1800, person>>::encode({{"ann", 30}});
// 1800({"age": 30, "name": "ann"})
```

## 6. Variants

`decode` gives a `std::variant` the first alternative that accepts the
item. An integer alternative accepts an integer that it can hold.
`encode` writes the alternative that the variant holds.

```cpp
using id = std::variant<std::uint8_t, std::int64_t, std::string>;
```

| bytes | `cbor::structs<id>::decode(bytes)` |
|---|---|
| `42` | index 0, the `std::uint8_t` 42 |
| `-1` | index 1, the `std::int64_t` -1 |
| `256` | index 1, the `std::int64_t` 256 |
| `"x"` | index 2, the `std::string` "x" |
| `true` | `error::incorrect_type` |

## 7. Shared values

`decode` reads tag 28 and tag 29 of the value-sharing registration with
a table that exists for one call. A tag 28 is allowed on every member and
keeps the value. A tag 29 needs a member that can share a value; a plain
member gives `incorrect_type`. A tag 29 that names a tag 28 in a skipped
member gives `sharedref_not_complete`, and a tag 29 that names no tag 28
before it gives `sharedref_index_not_marked`. Nothing is decoded twice.

```cpp
// [28("x"), 29(0)]
cbor::structs<std::vector<std::string>>::decode(bytes);   // incorrect_type
```

`encode` writes no shared value.

## 8. A memory budget

The form with a target reads into the containers of the target. A target
of `std::pmr` containers sets the memory resource of every element.
When the resource refuses, `decode` returns `error::not_enough_memory`.

```cpp
using texts = std::pmr::vector<std::pmr::string>;
auto const owner = std::make_shared<std::string const>(
    *cbor::structs<std::vector<std::string>>::encode({std::string(40, 'a'), std::string(40, 'b')}));
std::array<std::byte, 64> small{};
std::pmr::monotonic_buffer_resource budget(small.data(), small.size(), std::pmr::null_memory_resource());
cbor::structs<texts>::decode(owner, texts(&budget));   // not_enough_memory
```

`decode` does not reserve memory for a count that the input states. Each
element is made when it is read. [basics.md](basics.md), section 6,
describes the memory of every API.

## 9. Errors

| Input | Error |
|---|---|
| an integer that the C++ type cannot hold | `number_out_of_range` |
| a binary64 that a `float` cannot hold exactly | `number_out_of_range` |
| an item of another major type | `incorrect_type` |
| an array whose count differs from a `std::tuple` or `std::array` | `incorrect_type` |
| a variant with no alternative that accepts the item | `incorrect_type` |
| a missing key of a member that is not optional | `key_not_found` |
| a map key that is an array, a map or a tag | `unsupported_value` |
| bytes after the item | `syntax_error` |
| the input ends inside the item | `too_little_data` |
| a refused allocation | `not_enough_memory` |
