# mbedded-cbor

mbedded-cbor encodes and decodes CBOR (RFC 8949) for a host language
that embeds it. It is one C++23 header, `include/cbor/cbor.hpp`. The
host language answers questions about its values; the library makes
every decision.

## Build and test

The library is a header and needs no build. The tests use CMake and
doctest:

    cmake -S . -B build
    cmake --build build
    build/mbedded-cbor-test

A clone is recursive, because `test-vectors` is a submodule. When CMake
finds simdutf, text strings are checked for UTF-8; without it they are
not checked. RFC 8949 section 5.3.1 permits both.

`containers/` holds a Containerfile for openSUSE Tumbleweed, AlmaLinux,
Alpine and Nix. Each is built and run with podman.

`-DCBOR_FUZZ=ON` with clang builds `mbedded-cbor-fuzzer`, a libFuzzer
target. `fuzz/corpus` is its corpus.

## Decode

    std::expected<Host::value, cbor::error> cbor::decode<DepthMax>(host, bytes);

The library reads the bytes and calls one customization point of the
host for each data item. The host builds its own value and returns it:

| Data item | Customization point |
|---|---|
| unsigned integer | `unsigned_integer_decode(host, n)` |
| negative integer | `negative_integer_decode(host, n)`, the value is -1 - n |
| bignum, tags 2 and 3 | `unsigned_bignum_decode(host, magnitude)`, `negative_bignum_decode(host, magnitude)` |
| byte string, text string | `byte_string_decode(host, view)`, `text_string_decode(host, view)` |
| array | `array_decode(host)`, then `array_append(host, array, element)` |
| map | `map_decode(host)`, then `map_insert(host, map, key, value)` |
| tag | `tag_decode(host, number, content)` |
| floating-point, simple value | `float_decode(host, double)`, `simple_value_decode(host, n)` |

No size from the data reaches the host before the items arrive. Tags 28
and 29 (shared references) are always read. A host that cannot go on
throws; the library catches nothing.

## Encode

    std::expected<void, std::error_code> cbor::encode<DepthMax, Sharing>(host, writer, value);

The library asks `kind_of(host, value)` and then one question for that
kind: `unsigned_of`, `magnitude_of`, `bytes_of`, `text_of`, `float_of`,
`simple_of`, `array_size` and `array_at`, `map_size` and
`map_for_each`, or `registered_tag`. A host answers only the questions
its language has. A kind without an answer is `unsupported_value`.

The writer belongs to the host: `reserve(n)`, `append(bytes)` and
`done(size)`. `reserve` and `append` return
`std::expected<void, std::errc>`.

With `cbor::sharedrefs::on` the library makes two passes and writes
tags 28 and 29 for a value that occurs more than once, so a cycle
encodes. `value_identity` and `key_identity` tell which values can be
shared. With `cbor::sharedrefs::off` there is one pass and no code for
these tags.

## Registered tags

A host can register a tag for its own type. On encode the library calls
`before_encode(host, object)` once per object for the content. On
decode it calls `tag_begin(host, number)` for an empty object before the
content, then `registered_decode` and `after_decode`. The library fixes
the order of these calls.

## Parts of a document

- `cbor::doc_end<DepthMax>(bytes)` gives the size of the first complete
  data item, or `too_little_data` when the bytes end inside it.
- `cbor::lazy` is a view of a document and an offset. `lazy_at` indexes
  an array or finds a key of a map. `lazy_decode` decodes the value at
  the offset.
- `cbor::path_compile("$.users[*].name")` reads a path. `path_decode`
  follows it over a lazy view; each `[*]` gives an array.

## Errors

`cbor::error` converts to `std::error_code`. Each error belongs to one
`cbor::condition`: `not_well_formed` (RFC 8949 Appendix F),
`not_valid` (section 5.3), `not_supported` or `not_found`.

## License

Apache License 2.0, see `LICENSE`. `refs/rfc8949.txt` is RFC 8949,
verbatim, under the license of the IETF Trust.
