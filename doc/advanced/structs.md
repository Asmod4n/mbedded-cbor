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
`std::span<std::byte const>`. So `decode` always returns a
`ref<T>`, which holds the owner of the bytes. A view in the result
stays valid while the `ref` lives. A result with no view member
holds its own copy of every value.

| Call | Owner |
|---|---|
| `decode(std::string)` with an rvalue | the library moves the string into a new `std::shared_ptr<std::string const>` |
| `decode(std::shared_ptr<std::string const>)` | the caller, no copy |
| `decode(std::shared_ptr<std::string const>, target)` | the caller, and `decode` reads into `target` (section 8) |
| `decode(std::shared_ptr<void const> owner, part)` | the caller: `part` is a `std::string_view` into memory that `owner` keeps alive |
| `decode(std::shared_ptr<void const> owner, part, target)` | the same, and `decode` reads into `target` |

No form copies the bytes. A `std::string` lvalue, a `std::string_view`
alone and a `char const *` do not compile.

```cpp
// bytes: "abc"
auto const owner = std::make_shared<std::string const>(std::move(bytes));
cbor::structs<std::string_view>::decode(owner);         // a view into *owner
```

A null or an empty owner throws `std::logic_error`. A `std::string`
lvalue beside an owner does not compile: the bytes beside an owner are a
view, for example a `std::string_view`.

## 3. Every type

| C++ type | CBOR item |
|---|---|
| `bool` | `true`, `false` |
| `std::nullptr_t` | `null` |
| `cbor::simple_value` | a simple value |
| an integer of 8 to 64 bits | an integer |
| `__int128`, `unsigned __int128` | an integer, or a bignum (tag 2 or 3) |
| an enum with a fixed underlying type | the integer of its underlying type |
| `float`, `double` | the shortest float that holds the value exactly; a NaN keeps its bits |
| `std::string`, `std::pmr::string`, `std::string_view` | a text string |
| `std::span<std::byte const>` | a byte string |
| `std::vector<E>`, `std::array<E, N>` with E `std::byte` or an integer of 8 to 64 bits, `float` or `double` | an RFC 8746 typed array, little endian: tag 64 for `std::byte` and `std::uint8_t` |
| `std::set`, `std::flat_set` | tag 258 on an array (CBOR_SETS.md) |
| `std::chrono::sys_seconds` | tag 1 on an integer (RFC 8949 3.4.2) |
| `std::chrono::year_month_day` | tag 100 on an integer of days (RFC 8943) |
| `std::optional<E>` | the item of E, or `null` when empty |
| `std::variant<E...>` | the item of the alternative it holds |
| `std::tuple<E...>`, `std::array<E, N>` of other types | an array of exactly that count |
| `std::map`, `std::unordered_map` and their `std::pmr` forms | a map |
| `std::multimap`, `std::unordered_multimap` and their `std::pmr` forms | a map from each key to the array of its values: RFC 8949 5.6 makes a map with a repeated key invalid |
| `std::vector`, `std::deque`, `std::list` and other containers | an array |
| `cbor::tagged_item<N, E>`, E no struct | tag N on the item of E |
| a struct | a map, section 4 |

```cpp
cbor::structs<std::set<std::uint8_t>>::encode({3, 1});                          // 258([1, 3])
cbor::structs<std::array<std::byte, 2>>::encode({std::byte{1}, std::byte{2}});  // 64(h'0102')
cbor::structs<std::chrono::sys_seconds>::encode(std::chrono::sys_seconds{std::chrono::seconds{1700000000}});
// 1(1700000000)
```

`decode` takes each of these types only under its own tag. A typed
array takes only the little-endian tag of its element, and a
`std::array` only its own count. So a `std::vector` or a `std::array`
of a number type does not take a plain array: it gives
`error::incorrect_type`. A `std::set` takes the items of tag 258
in any order; an item that comes twice gives `error::duplicate_key`.
`std::chrono::sys_seconds` also takes a float under tag 1 when its value
is a whole number; any other float gives `error::number_out_of_range`.

Where the standard library has `std::chrono::parse` (libstdc++), a
`std::chrono::sys_time` also takes the RFC 3339 date-time of tag 0, and a
`std::chrono::year_month_day` the RFC 3339 full-date of tag 1004. The
letters `T` and `Z` may be lower case. A fraction of a second needs a
`sys_time` that holds it, for example `sys_time<milliseconds>`; a
`sys_seconds` gives `error::incorrect_type` for it. Second 60 gives
`error::incorrect_type`. libc++ has no `std::chrono::parse`, so tags 0
and 1004 give `error::incorrect_type` there; a program that needs them
brings a type of its own.

An enum with no fixed underlying type does not compile in `decode`,
because some values of its underlying type are not values of the enum.

`encode` writes a NaN in the shortest width that holds its bits exactly
(RFC 8949 4.1). A NaN whose low 42 bits of a binary64 are zero is a
binary16, one whose low 29 bits are zero is a binary32, and any other
NaN is a binary64. A `float` NaN keeps its own bits. `decode` gives the
NaN back with the same bits.

```cpp
// a double NaN with the bits 7ff8000000000000 is f9 7e00
// a double NaN with the bits 7ff8000020000000 is fa 7fc00001
// a double NaN with the bits 7ff8000000000001 is fb 7ff8000000000001
```

A `float` takes a binary16, binary32 or binary64 item. A binary64 item
that a `float` cannot hold exactly gives `error::number_out_of_range`.
A binary64 NaN whose low 29 bits are not zero is such an item.

`encode` throws `std::logic_error` for a `std::chrono::year_month_day`
that is not a valid date. When an allocation is refused, `encode` throws
`std::bad_alloc`, and `decode` returns `error::not_enough_memory`.

## 4. Structs and keys

A struct is a map. Each public data member is one entry. The keys
come in the order of RFC 8949 4.2.1: sorted by their encoded bytes.

An aggregate with no description is read by the names of its members:
through reflection on a build with `-freflection`, through Boost.PFR on
other builds. It takes no annotation, so it gives the same bytes on
every build: `[[=cbor::key("x")]]` and `[[=cbor::tag(n)]]` on a type
that `structs` reads do not compile. An aggregate with a base class
needs a description. [cbor::schema](schema.md) reads the same
description.

A description gives the tag and the members of a struct or a class.
`CBOR_DEFINE_TYPE(T, tag, members...)` writes it at global scope. The
macro writes this specialization of `cbor::describe_members<T>`:

```cpp
template <>
struct cbor::describe_members<T> {
    static constexpr std::optional<cbor::tag> tag = /* tag */;
    static constexpr std::tuple members{/* members... */};
};
```

`tag` is a `std::optional<cbor::tag>`, and `members` is a `std::tuple`
of `cbor::member` entries.
The tag `cbor::untagged` gives a map with no tag, an untagged message in
the words of RFC 9052 2 (SenML, CTAP2, the COSE header). A decode of
such a type refuses a tag in front of the map with
`error::incorrect_type`.

| Entry | Key |
|---|---|
| `CBOR_MEMBER(T, m)` | the text of the name `m` |
| `m` in `CBOR_DEFINE_TYPE_MEMBERS(T, tag, m...)` | the text of the name `m` |
| `cbor::member{"x", &T::m}` | the text `x` |
| `cbor::member{4, &T::m}`, `cbor::member{-1, &T::m}` | the integer |

- The description stands outside every namespace, after the type and
  after the description of each struct that the type holds.
- `CBOR_DEFINE_TYPE_MEMBERS(T, tag, m...)` takes up to 343 names.
- `CBOR_DEFINE_TYPE`, `CBOR_DEFINE_TYPE_MEMBERS` and `CBOR_MEMBER` take a
  type alias for a type
  whose name holds a comma, because the preprocessor splits the
  arguments of a macro at each comma.
- An entry names a public data member of the type or of a public base.
- On a build with `-freflection`, the entries are exactly the public data
  members of the type and of its public bases. Another list does not
  compile.
- A member that comes twice, two members with one key, a tag that the
  library takes for a standard type (for example 1, 258 or a tag of
  RFC 8746) and a tag that the IANA registry calls always invalid do not
  compile.
- A description is checked where `encode`, `decode` or the size of the
  type is compiled. A description that nothing uses is not checked.
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

CBOR_DEFINE_TYPE(account, cbor::tag{160061}, CBOR_MEMBER(account, user), cbor::member{1, &account::level});

// an account with user "ann" and level 2 is 160061({1: 2, "user": "ann"})

struct senml {          // RFC 8428 6: n is 0, v is 2
    std::string n;
    double v;
};

CBOR_DEFINE_TYPE(senml, cbor::untagged, cbor::member{0, &senml::n}, cbor::member{2, &senml::v});

// senml{"t", 1.5} is {0: "t", 2: 1.5}
```

`decode` reads the entries of a map in any order:

- a key that no member has: the entry is skipped;
- a key of a member that comes twice: `error::duplicate_key` (RFC 8949
  5.6); a `std::map`, a `std::unordered_map` and a `std::flat_map` give
  the same error on a key that comes twice;
- a missing key of a member that is not optional: `error::key_not_found`;
- a key that is an array, a map or a tag: `error::unsupported_value`.

## 5. Tags

A described struct carries the tag of its description (section 4).
`cbor::tagged_item<N, E>` (RFC 8949 3.4, "tagged data item") writes tag
N on the item of E, for content that is not a struct, for example the
array of COSE_Sign1 (RFC 9052 4.2). Over a struct it does not compile.
A struct that carries the annotation `[[=cbor::tag(n)]]` does not
compile in `structs`.

```cpp
struct person { std::string name; std::int64_t age; };

CBOR_DEFINE_TYPE_MEMBERS(person, cbor::tag{1800}, name, age);

cbor::structs<person>::encode({"ann", 30});
// 1800({"age": 30, "name": "ann"})
```

## 6. Variants

`decode` gives a `std::variant` the first alternative that accepts the
item. An integer alternative accepts an integer that it can hold.
`encode` writes the alternative that the variant holds.

```cpp
using id = std::variant<std::uint8_t, std::int64_t, std::string>;
```

| bytes | `cbor::structs<id>::decode(std::move(bytes))` |
|---|---|
| `42` | index 0, the `std::uint8_t` 42 |
| `-1` | index 1, the `std::int64_t` -1 |
| `256` | index 1, the `std::int64_t` 256 |
| `"x"` | index 2, the `std::string` "x" |
| `true` | `error::incorrect_type` |

## 7. Shared values

`decode` reads tag 28 and tag 29 of the value-sharing registration with
a table that exists for one call. A tag 28 is allowed on every member and
keeps the value. No member takes a tag 29, so every tag 29 gives an
error:

| The tag 29 names | Error |
|---|---|
| no tag 28 before it | `sharedref_index_not_marked` |
| a tag 28 in a skipped member | `sharedref_not_complete` |
| any other tag 28 | `incorrect_type` |

Nothing is decoded twice.

```cpp
// [28("x"), 29(0)]
cbor::structs<std::vector<std::string>>::decode(std::move(bytes));   // incorrect_type
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
| a plain array for a `std::vector` or a `std::array` of a number type | `incorrect_type` |
| a variant with no alternative that accepts the item | `incorrect_type` |
| a missing key of a member that is not optional | `key_not_found` |
| a map key that is an array, a map or a tag | `unsupported_value` |
| the input ends inside the item | `too_little_data` |
| a refused allocation | `not_enough_memory` |

`decode` reads the first item and does not read the bytes after it.

| `encode` of | Result |
|---|---|
| a `std::chrono::year_month_day` that is not a valid date | throws `std::logic_error` |
| any value, when an allocation is refused | throws `std::bad_alloc` |
