# Internals

This page states how the library reads, for a person who changes the
library. An application does not depend on it.

## 1. How the documents are tested

Every example of a document in `doc/` is a doctest case. The cases of
`doc/<name>.md` are in `test/doc_<name>.cpp`, and their names start with
`doc <name>:`. The cases of `doc/advanced/<name>.md` are in
`test/doc_advanced_<name>.cpp`, and their names start with
`doc advanced <name>:`. The cases of this page are in
`test/doc_internals.cpp`, and their names start with `doc internals:`. A
test builds the input that a comment of the document shows as text with
`preferred()` of `test/reference.hpp`. The cases of
`doc/reference/<group>/` are in `test/doc_reference_<group>.cpp`, and
their names start with `doc reference <group>:`. A claim
about allocations has its test in `test/allocation.cpp`. `test/cts.cpp`
runs the JSONPath Compliance Test Suite.

The reference site, <https://asmod4n.github.io/mbedded-cbor/>, is built
from `doc/reference`. Each `example` block of a page runs in the browser,
in the library compiled to WebAssembly. The build of the site runs each
example and compares its result with the page.

Each push runs nine Linux builds, two macOS builds and a test that
decodes the output of the library with other decoders. `test/run.sh`
names the Linux builds. With `CBOR_FUZZ`, CMake builds one fuzz target
for each public entry: `decode`, `diagnostic`, `encode`, `encoder`,
`item_size`, `lazy`, `path`, `sequence_reader` and `structs_decode`, and
with reflection also `schema_decode` and `schema_encode`.

## 2. A lazy reads heads

`lazy::take_ownership` and `lazy::view` read one head at most: a leading
tag 55799, which they skip. A step reads the heads that it needs when it
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

## 4. The limits are constants

Each limit is a `static constexpr` member of `cbor::limits`. A check of
a string length or an element count stands under `if constexpr` on its
limit, so a build that sets no size limit has no such check. A skip
uses `heads::decoder<false>`, which checks no size limit, because the
limits count only what a call reads.

## 5. Tags 28 and 29

A lazy keeps no marks of tag 28. It reads tags 28 and 29 as plain tags, so
a read changes nothing in it and needs no lock. Only `cbor::structs`
decode resolves them, with a table of the tags 28 of one call.

## 6. A compile-time path

The compiler parses a compile-time path once. At run time it is a set of
constant arrays.

## 7. The item tree

`lazy::clone` builds one `cbor::item` for each data item in a
`std::pmr::monotonic_buffer_resource` that the result owns. The children
of an array or a map stand by value in one block of that resource, so a
container holds a `std::span<cbor::item const>`. A text or a byte string
is copied into the resource. The first block is sized from the length of
the input, up to a bound. A count is checked against the bytes that
are left before an element is read, and the sum of all open counts never
exceeds the bytes that are left.

A tag 28 and a tag 29 are tag items over their content. The resource
holds one sorted table: each tag 24 item with the root of its embedded
item. `cbor::encoded_cbor_data_item` searches the table. The result is a
`cbor::owning<cbor::item>`. Its `std::shared_ptr` has a deleter that owns
the resource, and the function finds the resource through
`std::get_deleter`. `encode` of an item
writes each head with the additional information that the item keeps, a
float at the width of its type,
and does not follow a tag 29. A NaN keeps its sign and payload.

## 8. The width of a float

`structs::encode` writes a float in the shortest width that holds it
exactly (RFC 8949, section 4.1). A NaN keeps its bits. A `double` NaN
takes the half width when the low 42 bits of its significand are zero,
the single width when the low 29 bits are zero, and else the double
width. A `float` NaN keeps its own bits: it takes the half width when the
low 13 bits of its significand are zero, and else the single width. The
decoder pads the significand of a shorter NaN with zeros on the right, so
a NaN reads back with the bits that were written. `cbor::schema` writes a
float at the width of its type and copies its bits.

## 9. The schema reads the descriptions

`cbor::schema<T>` takes the members, the keys and the tag of each struct
from `cbor::describe_members<T>`, which `CBOR_DEFINE_TYPE` or `CBOR_DEFINE_TYPE_MEMBERS` writes. It
needs no reflection to lay out, write or read a message, so one class
builds on every compiler. With reflection, `schema` also compares each
description with the public members that reflection reads, with
`std::meta::access_context::unprivileged()`. Without reflection,
Boost.PFR counts the members of an aggregate where it is available.

For a list of fixed-size structs, a compile-time probe decides how
`encode` writes the elements. The probe is a `std::bit_cast` of a struct
whose bytes are zero except for the bytes of one member at its offset in
the message. Where every member reads back those bytes, on a little
endian machine, the struct in memory has the bytes of the element. Then
`encode` writes a contiguous list with one copy. Else it writes each element
field by field.

`path`, `at_path` and `decode` read a message from both ends: the table
from the start and the root from the end. A check can fail on a message
whose start is valid and whose end does not hold the tag of the root.
Then they skip the items of the message once. If the bytes end before
the skip ends, the error is `too_little_data`. So a
message that ends too early gives `too_little_data` and not the error of
the first check that failed. The skip runs on the path of an error only.
