# Packed in detail

This page adds the details to [../packed.md](../packed.md). The examples
use these structs:

```cpp
struct [[=cbor::tag(1700)]] tire {
    std::uint16_t diameter;
    float pressure;
};

struct [[=cbor::tag(1701)]] car {
    std::string make;
    std::uint8_t seats;
    std::vector<tire> tires;
    std::vector<double> track;
    std::optional<std::string> note;
};

car const sample{"Volvo", 5, {{17, 2.5f}, {18, 2.25f}}, {1.0, 2.0, 3.0}, std::nullopt};
auto const bytes = *cbor::packed<car>::encode(sample);   // a std::string
```

## 1. The message

A message is one tag 113 (Basic-Packed-CBOR, draft-ietf-cbor-packed-19)
on an array of a table and a rump.

- The table holds the keys of each struct as a tag 114 (the record
  function), a directory of the offsets of the parts of variable size,
  and those parts: texts, byte strings and lists.
- The rump is the root struct. It starts with the tag of its struct. Each
  field has a head of fixed width, so a number takes the same count of
  bytes for every value.
- A list of a fixed-width number is a typed array of RFC 8746 in little
  endian.

A decoder of Packed CBOR reads the message as a map with the names of the
members as keys. `cbor::item_size` of a message is the size of the
message.

## 2. Structs and tags

The tag numbers of the root and of every struct inside it differ.
`cbor::tags_registered<T>()` is true when this holds. A struct with no
tag, or two structs with one tag, leave no `packed<T>` to call: the
program does not compile.

`[[=cbor::key("x")]]`, `[[=cbor::skip{}]]` and `[[=cbor::allowlist{}]]`
with `[[=cbor::allow{}]]` choose the keys and the members as in
[structs.md](structs.md), section 4. Only public members are written.

## 3. Encode and decode

| Call | Result |
|---|---|
| `packed<T>::encode(value)` | `std::expected<std::string, error>` |
| `packed<T>::encode(value, target)` | `std::expected<std::size_t, error>`, the count of bytes appended |
| `packed<T>::decode(bytes)` | `std::expected<owning_ref<T>, error>`, the whole struct |

`decode` takes the same four forms as `structs<T>::decode`: a moved
`std::string`, a `std::string_view` that it copies, an owner with its
bytes, and an owner with its bytes and a target
([structs.md](structs.md), section 2). A target of `std::pmr`
containers sets the memory of the result ([basics.md](basics.md),
section 6).

## 4. One value with no accessor

`packed<T>::at_path<Path>(encoded, index...)` reads one value. It makes
every check of an accessor and keeps nothing. It exists only where the
value holds no view into the message: a number, a `bool`, an enum, or an
optional of one.

```cpp
cbor::packed<car>::at_path<"$.seats">(bytes);                      // 5
cbor::packed<car>::at_path<"$.tires[1].diameter">(bytes);          // 18
cbor::packed<car>::at_path<"$.tires[].pressure">(bytes, 0uz);      // 2.5f
cbor::packed<car>::at_path<"$.track[2]">(bytes);                   // 3.0
cbor::packed<car>::at_path<"$.tires[].diameter">(bytes, 5uz);      // error::index_out_of_bounds
```

A path is a string literal. It names members with `.name` and elements
with `[n]`. `[]` takes its index from the next argument, a `std::size_t`,
so the count of arguments equals the count of `[]`. This path is not
JSONPath: it has no wildcard, no filter and no descendant segment.

A path to a member that `T` does not have, a wrong count of arguments, a
path to a text, or a path to a member that is not trivially copyable,
such as a `std::vector` of structs, does not compile.

## 5. The accessor

`packed<T>::path(...)` opens a message and gives the root accessor,
`packed<T>::accessor<T>`. The forms are those of `decode`:

| Call | Owner |
|---|---|
| `path(std::string)` with an rvalue | the library moves the string |
| `path(std::string_view)` | the library copies the bytes |
| `path(std::shared_ptr<std::string const>)` | the caller, no copy |

An accessor holds the owner. Every value and every accessor that it gives
holds the same owner, so an accessor of a list outlives the temporary
root that gave it.

```cpp
auto const root = cbor::packed<car>::path(bytes);
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

A `std::string_view` stays valid while an accessor of the same owner
lives. `at_path` on a temporary accessor does not compile. An empty owner
throws `std::logic_error`. An accessor keeps its view after a move.

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
  inside it, so a message of another layout gives `incorrect_type`;
- that the directory lies inside the message;
- the tag of the struct of the root.

Each `at_path` checks at each read:

- the index of each step against the count of the list;
- that each offset that it reads from the directory lies inside the
  message;
- the tag of the struct of each struct on the path;
- the head of the field that it reads;
- that a part of variable size lies inside the message and does not
  overlap another part.

`view` checks the tag and the length of the typed array once. After that,
each element read checks only its index.

`packed<T>::at_path` with no accessor makes the checks of `path` and of
`at_path` in one call.

## 8. Errors

| Input | Error |
|---|---|
| a message shorter than the root | `too_little_data` |
| a message of another layout or struct, or a forged tag of a struct | `incorrect_type` |
| a forged head of a field | `incorrect_type` |
| a typed array whose length is not a multiple of its element size | `inadmissible_type_for_tag_content` |
| an index past the end of a list | `index_out_of_bounds` |
| a reference to an entry that the table does not have | `unpopulated_table_index` |
| a refused allocation in `decode` | `not_enough_memory` |

```cpp
cbor::packed<car>::decode(std::string_view(bytes).substr(0, 3));     // too_little_data
cbor::packed<car>::decode(*cbor::packed<tire>::encode(tire{1, 1}));  // too_little_data
cbor::packed<tire>::decode(bytes);                                   // incorrect_type
```

## 9. Cost

| Call | Time | Memory |
|---|---|---|
| `path(std::shared_ptr<std::string const>)` | independent of the size of the message | none |
| `path(std::string_view)` | the copy of the message | the copy |
| `accessor::at_path`, `view` | one step for each part of the path | none |
| `packed<T>::at_path` | the directory and one step for each part of the path | none |
| `decode` | the size of the message | the result |
| `encode` | the size of the message | the message |

[complexity.md](complexity.md) gives the O form of each function.
