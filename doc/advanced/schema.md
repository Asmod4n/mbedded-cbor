# Schema in detail

This page adds the details to [../schema.md](../schema.md).
`cbor::schema<T>` is one class for every compiler: it builds with C++23
on g++ 15, g++ 16, clang 23 and AppleClang. With C++26 reflection (g++ 16
with `-std=c++26 -freflection`) it also checks each description against
the members that reflection reads. The examples use these structs:

```cpp
struct tire {
    std::uint16_t diameter;
    float pressure;
};

struct car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
    std::optional<std::string> note;
};

CBOR_DEFINE_TYPE(tire, cbor::tag{1700}, CBOR_MEMBER(tire, diameter), CBOR_MEMBER(tire, pressure));
CBOR_DEFINE_TYPE(car, cbor::tag{1701},
                 CBOR_MEMBER(car, make), CBOR_MEMBER(car, seats), CBOR_MEMBER(car, tires),
                 CBOR_MEMBER(car, track), CBOR_MEMBER(car, note));

car const sample{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}, std::nullopt};
auto const owner = std::make_shared<std::string const>(*cbor::schema<car>::encode(sample));
std::string_view const bytes = *owner;                    // the message, read in place
```

## 1. The message

A message is one tag 113 (Basic-Packed-CBOR, draft-ietf-cbor-packed-19)
on an array of a table and a rump.

- The table holds the keys of each struct as a tag 114 (the record
  function). For each struct that is the element of a list of fixed-size
  structs, it holds a description: the tag of the struct and the key and
  RFC 8746 type of each member. Then come a directory of the offsets of
  the parts of variable size, and those parts: texts, byte strings and
  lists.
- The rump is the root struct. It starts with the tag of its struct. Each
  field has a head of fixed width, so a number takes the same count of
  bytes for every value. A float field holds the bits of the value at the
  width of its type, so a NaN keeps its bits.
- A list of a fixed-width number is a typed array of RFC 8746 in little
  endian.
- A list of fixed-size structs, such as `tires`, is one byte string of
  its elements under tag 32873: little endian, with no padding.

A decoder of Packed CBOR reads the message as a map with the names of the
members as keys. `cbor::item_size` of a message is the size of the
message. [reference/schema/wire.md](../reference/schema/wire.md) shows
each byte.

`cbor::schema<T>::description()` gives the 114 entries and the
descriptions. They are the same in every message of `T`. With
`cbor::description_place::apart`, `encode` writes the message without
them, and only a read with `apart` reads that message (section 3).

## 2. Structs and tags

The root and every struct inside it have a description with a tag.
`CBOR_DEFINE_TYPE(type, tag, members...)` writes it at global scope. The
tag numbers differ. `cbor::tags_registered<T>()` is true when this holds.
A struct with no description, or two structs with one tag, do not
compile in `schema<T>`.

The macro writes a specialization of `cbor::describe_members`. For
`tire`, it writes:

```cpp
template <>
struct cbor::describe_members<tire> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1700};
    static constexpr std::tuple members{CBOR_MEMBER(tire, diameter), CBOR_MEMBER(tire, pressure)};
};
```

The description gives the key of each member:

| Entry | Key |
|---|---|
| `CBOR_MEMBER(T, m)` | the name `m` |
| `cbor::member{"x", &T::m}` | the text `x` |

```cpp
struct chosen {
    std::uint8_t number;
    std::uint8_t kept;
};

CBOR_DEFINE_TYPE(chosen, cbor::tag{1710}, cbor::member{"id", &chosen::number}, CBOR_MEMBER(chosen, kept));

auto const message = cbor::schema<chosen>::encode(chosen{7, 8});
cbor::schema<chosen>::at_path<"$.id">(*message);   // 7
```

These do not compile: a key that is not a text, two members with one key,
a member listed twice, a member of a base class, `cbor::untagged`, and a
tag that a standard type has. With reflection, a member that the
description leaves out and a class with a base class do not compile
either. Without reflection, Boost.PFR counts the members of an aggregate
where it is available, and a count that differs does not compile.

A class with no default constructor is read through a constructor that
takes the described members in the order of the description.

## 3. Encode and decode

| Call | Result |
|---|---|
| `schema<T>::encode(value)` | `std::expected<std::string, error>` |
| `schema<T>::encode(value, target)` | `std::expected<std::size_t, error>`, the count of bytes appended |
| `schema<T>::decode(owner)` | `std::expected<ref<T>, error>`, the whole struct |

`decode` takes the same forms as `structs<T>::decode`: a moved
`std::string`, a `std::shared_ptr<std::string const>`, a
`std::shared_ptr<void const>` with a view of the bytes, and each of the
last two with a target ([structs.md](structs.md), section 2). No form copies the bytes. A target of `std::pmr`
containers sets the memory of the result ([basics.md](basics.md),
section 6).

`encode` writes a list of fixed-size structs with one copy where the
struct in memory has the bytes of the element: little endian, no
padding, and each member at its offset. `tire` has two bytes of padding,
so `encode` writes each of its elements field by field. A compile-time
probe finds the layout, on every compiler.

Each call takes `cbor::description_place` as a template argument: the
first one, or the one after the path in `at_path`. The default is
`inside`.

```cpp
constexpr auto apart = cbor::description_place::apart;
auto const data = cbor::schema<car>::encode<apart>(sample);
cbor::schema<car>::decode<apart>(std::string(*data));                        // the car
data->size() + cbor::schema<car>::description().size() == bytes.size();     // true
```

## 4. One value with no accessor

`schema<T>::at_path<Path>(encoded, index...)` reads one value. It makes
every check of an accessor and keeps nothing. It exists only where the
value holds no view into the message: a number, a `bool`, an enum, or an
optional of one.

```cpp
cbor::schema<car>::at_path<"$.seats">(bytes);                      // 5
cbor::schema<car>::at_path<"$.tires[1].diameter">(bytes);          // 18
cbor::schema<car>::at_path<"$.tires[].pressure">(bytes, 0uz);      // 2.5f
cbor::schema<car>::at_path<"$.track[2]">(bytes);                   // 3.0
cbor::schema<car>::at_path<"$.tires[].diameter">(bytes, 5uz);      // error::index_out_of_bounds
```

A path is a string literal. It names members with `.name` and elements
with `[n]`. `[]` takes its index from the next argument, a `std::size_t`,
so the count of arguments equals the count of `[]`. This path is not
JSONPath: it has no wildcard, no filter and no descendant segment.

On a byte string, a `std::array` or a `std::vector` of `std::uint8_t` or
`std::byte`, `[n]` reads one byte:

```cpp
struct frame {
    std::array<std::uint8_t, 4> code;
    std::vector<std::byte> payload;
};

CBOR_DEFINE_TYPE(frame, cbor::tag{1711}, CBOR_MEMBER(frame, code), CBOR_MEMBER(frame, payload));

auto const packet = cbor::schema<frame>::encode(frame{{1, 2, 3, 4}, {std::byte{9}, std::byte{8}}});
cbor::schema<frame>::at_path<"$.code[2]">(*packet);      // 3
cbor::schema<frame>::at_path<"$.payload[1]">(*packet);   // std::byte{8}
cbor::schema<frame>::at_path<"$.payload[]">(*packet, 2uz);   // error::index_out_of_bounds
```

A path to a member that `T` does not have, a wrong count of arguments, a
path to a text, a constant index past the end of a `std::array`, or a
path to a member that is not trivially copyable, such as a
`std::vector` of structs, does not compile.

## 5. The accessor

`schema<T>::path(...)` opens a message and gives the root accessor,
`schema<T>::accessor<T>`. The forms are those of `decode`:

| Call | Owner |
|---|---|
| `path(std::string)` with an rvalue | the library moves the string |
| `path(std::shared_ptr<std::string const>)` | the caller, no copy |
| `path(std::shared_ptr<void const> owner, part)` | the caller: `part` is a `std::string_view` into memory that `owner` keeps alive |

An accessor holds the owner. Every value and every accessor that it gives
holds the same owner, so an accessor of a list outlives the temporary
root that gave it.

```cpp
auto const root = cbor::schema<car>::path(owner);
root->at_path<"$.make">();                  // "Volvo", a std::string_view
auto const tires = root->at_path<"$.tires">();
tires->size();                              // 2
auto const second = tires->at_path<"@[]">(1uz);
second->at_path<"@.diameter">();            // 18
second->at_path<"$.seats">();               // 5
root->at_path<"$.note">();                  // an empty std::optional<std::string_view>
```

A path that starts with `$` starts at the root. A path that starts with
`@` starts at the part that the accessor names. `size()` gives the count
of elements of a list or a map.

The accessor of a list of fixed-size structs is a
`std::ranges::random_access_range` of element accessors:

```cpp
for (auto const t : *tires | std::views::drop(1))
    t.at_path<"@.pressure">();              // 2.25f
```

A `std::string_view` stays valid while an accessor of the same owner
lives. `at_path` on a temporary accessor does not compile. An empty owner
throws `std::logic_error`. An accessor keeps its view after a move, and
`at_path` on the accessor that it was moved from throws
`std::logic_error`.

## 6. Typed arrays

`accessor<T>::view<Path>(index...)` gives a `typed_array_view<E>` of a list
of a fixed-width number E. The view holds the owner and reads each element
from the bytes of the message.

```cpp
auto const track = root->view<"$.track">();
track->size();                                          // 3
std::accumulate(track->begin(), track->end(), 0.0);     // 6.0
(*track)[1];                                            // 2.0
track->at(3);                                           // error::index_out_of_bounds
(*track)[3];                                            // throws std::logic_error
track->last(2)->front();                                // 2.0
track->first(4);                                        // error::index_out_of_bounds
std::ranges::fold_left(*track, 0.0, std::plus<>{});     // 6.0
```

`typed_array_view<E>` is a `std::ranges::random_access_range` and a
`std::ranges::sized_range`. It has `size`, `empty`, `begin`, `end`,
`rbegin`, `rend`, `front`, `back`, `first`, `last`, `at` and
`operator[]`. Each element is a value of E, not a reference. `at`,
`first` and `last` give an error as a value. `operator[]`, `front` and
`back` throw `std::logic_error` outside the view. `begin` and `end` do
not compile on a temporary view.

## 7. What is checked once and what at each read

`path` checks once:

- that the message is at least as long as the table and the root;
- that the start of the table holds the keys of `T` and of each struct
  inside it, and the descriptions, so a message of another layout gives
  `incorrect_type`;
- that the directory lies inside the message;
- the tag of the struct of the root.

With `apart`, the message holds no keys. `path` then checks the start of
the message, the count of the table and the tag of the root.

Each `at_path` checks at each read:

- the index of each step against the count of the list;
- that each offset that it reads from the directory lies inside the
  message;
- the tag of the struct of each struct on the path;
- the head of the field that it reads;
- that a part of variable size lies inside the message and does not
  overlap another part;
- for a list of fixed-size structs, the tag 32873 and its reference, and
  that the length is a multiple of the size of the element.

`view` checks the tag and the length of the typed array once. After that,
each element read checks only its index.

`schema<T>::at_path` with no accessor makes the checks of `path` and of
`at_path` in one call.

The `unverified_` forms make only the checks of a bound: that each head,
length, item and directory entry lies inside the message, each index
against its count, and the limits. They skip the tags of the structs, the
heads of the fields, the start of the table, the fillers, the tag and the
length check of a typed array, the prefix of a list of fixed-size
structs, the check of a `bool` byte and, in `unverified_decode`, the
order of the items. An accessor from `unverified_path` gives only
unverified accessors.
[reference/schema/unverified.md](../reference/schema/unverified.md) has
the full list.

## 8. Errors

| Input | Error |
|---|---|
| a message that ends too early | `too_little_data` |
| a message of another layout or struct, or a forged tag of a struct | `incorrect_type` |
| a forged head of a field | `incorrect_type` |
| a typed array or a list of fixed-size structs whose length is not a multiple of its element size | `inadmissible_type_for_tag_content` |
| an index past the end of a list | `index_out_of_bounds` |
| a reference to an entry that the table does not have | `unpopulated_table_index` |
| a refused allocation in `decode` | `not_enough_memory` |

A message that ends too early gives `too_little_data` in `path`,
`at_path` and `decode`, also where the first check that fails is
another check. The unverified forms give the error of the check that
fails.

```cpp
cbor::schema<car>::decode(std::string(owner->substr(0, 3)));            // too_little_data
cbor::schema<car>::at_path<"$.seats">(bytes.substr(0, bytes.size() - 1)); // too_little_data
cbor::schema<car>::decode(*cbor::schema<tire>::encode(tire{1, 1}));     // too_little_data
cbor::schema<tire>::decode(owner);                                      // incorrect_type
```

## 9. Cost

| Call | Time | Memory |
|---|---|---|
| `path(std::shared_ptr<std::string const>)` | independent of the size of the message | none |
| `path(std::string&&)` | independent of the size of the message | one allocation for the owner |
| `accessor::at_path`, `view` | one step for each part of the path | none |
| `schema<T>::at_path` | the directory and one step for each part of the path | none |
| `decode` | the size of the message | the result |
| `encode` | the size of the message | the message |

An error of `path`, `schema<T>::at_path` or `decode` can cost a walk over
the message, which finds out whether the message ends too early.
[complexity.md](complexity.md) gives the O form of each function.
