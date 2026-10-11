# Internals

This page states how the library reads, for a person who changes the
library. An application does not depend on it.

## 1. How the documents are tested

Every example of a document in `doc/` is a doctest case. The cases of
`doc/<name>.md` are in `test/doc_<name>.cpp`, and their names start with
`doc <name>:`. The cases of `doc/advanced/<name>.md` are in
`test/doc_advanced_<name>.cpp`, and their names start with
`doc advanced <name>:`. A test builds the input that a comment of the
document shows as text with `encoded()` of `test/binding.hpp`. A claim
about allocations has its test in `test/allocation.cpp`. `test/cts.cpp`
runs the JSONPath Compliance Test Suite.

## 2. A lazy reads heads

`lazy::from` reads no head. A step reads the heads that it needs when it
is called: the heads from the start of its map or array to the target,
and the head of the target. It reads no byte after the head of the
target. So a target that the input does not hold gives `too_little_data`
at the step.

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

## 5. The marks of tag 28

The lazy values of one top-level item share one list of the positions of
its tags 28. The first tag 29 that a top-level item reads finds the tags
28 before it, once, and keeps their positions. A later tag 29 reuses them.
The list grows with no lock, so a lazy belongs to one thread. A debug
build records the thread that reads the list first, and a read of the
list in another thread throws `std::logic_error`.

A typed read of a path over bytes reads an item again through a lazy
when it meets a tag 29, so that it finds the marks.

## 6. A compile-time path

The compiler parses a compile-time path once. At run time it is a set of
constant arrays.

## 7. A binding

`decode.hpp` lists the calls that the library makes on a binding for each
kind of item. `encode.hpp` lists the calls of an encode.
