// Gives cbor::typed_array, a typed array tag and its bytes. Gives the
// tag numbers 64 to 87 and the bit fields of a typed array tag.
// https://www.rfc-editor.org/rfc/rfc8746#section-2
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace cbor
{

/// An RFC 8746 typed-array tag and a view of its encoded bytes.
/// The caller keeps the bytes alive while the view is used.
struct typed_array {
    /// RFC 8746 tag number identifying the element type and byte order.
    std::uint64_t tag;
    /// Encoded element bytes; does not own the storage.
    std::span<std::byte const> bytes;
};

class rfc8746
{
    enum class tag_number : std::uint64_t {
        typed_array_first = 64,
        typed_array_reserved = 76,
        float128_big_endian = 83,
        float128_little_endian = 87,
        typed_array_last = 87
    };

    enum class bit_field : std::uint8_t { ll = 0, e = 2, s = 3, f = 4 };

    friend class validity;

    friend class heads;

    friend class reflection;

    friend struct lazy;
};

}
