# Internals

This page states how the library reads, for a person who changes the
library. An application does not depend on it.

## 1. How the documents are tested

Every example of a document in `doc/` is a doctest case. The cases of
`doc/<name>.md` are in `test/doc_<name>.cpp`, and their names start with
`doc <name>:`. The cases of `doc/advanced/<name>.md` are in
`test/doc_advanced_<name>.cpp`, and their names start with
`doc advanced <name>:`. A test builds the input that a comment of the
document shows as text with `encoded()` of `test/reference.hpp`. A claim
about allocations has its test in `test/allocation.cpp`. `test/cts.cpp`
runs the JSONPath Compliance Test Suite.

## 2. A lazy reads heads

`lazy::copy` and `lazy::view` read no head. A step reads the heads that it needs when it
is called: the heads from the start of its map or array to the target,
and the head of the target. It reads no byte after the head of the
target. So a target that the input does not hold gives `too_little_data`
at the step. A map or an array that claims more items than the bytes
left gives `too_little_data` before the step reads an element.

## 3. A skip keeps a count

A skip reads heads only and jumps over the content of a string. It keeps
one count of the items that are still to skip, and no stack. It stops
with `too_little_data` when an item claims more than the bytes that are
left. So the nesting depth does not bound a skip, and `item_size` uses
the same skip.

## 4. The limits are read once

Each public entry reads `cbor::limits` once and passes the values down.
Each field holds one `std::atomic<std::size_t>`, read with relaxed
order. When the three size limits are `SIZE_MAX`, an entry takes the
form of the code that checks no size.

## 5. Tags 28 and 29

A lazy keeps no marks of tag 28. It reads tags 28 and 29 as plain tags, so
a read changes nothing in it and needs no lock. Only `cbor::structs`
decode resolves them, with a table of the tags 28 of one call.

## 6. A compile-time path

The compiler parses a compile-time path once. At run time it is a set of
constant arrays.

## 7. The item tree

`lazy::decode` builds one `cbor::item` for each data item in a
`std::pmr::monotonic_buffer_resource` that the result owns. The children
of an array or a map stand by value in one block of that resource, so a
container holds a `std::span<cbor::item const>`. The first block is sized
from the length of the input. A count is checked against the bytes that
are left before an element is read, and the sum of all open counts never
exceeds the bytes that are left.

A tag 28 and a tag 29 are tag items over their content. The resource
holds one sorted table: each tag 24 item with the root of its embedded
item. `cbor::encoded_cbor_data_item` searches the table. The result is a
`std::shared_ptr` whose deleter owns the resource, and the function finds
the resource through `std::get_deleter`. `encode` of an item
writes each head with the additional information that the item keeps, a
float at the width of its type,
and does not follow a tag 29.
