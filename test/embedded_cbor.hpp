#pragma once

#include <cbor/diagnostic_notation.hpp>
#include <cbor/error.hpp>

#include <cstddef>
#include <expected>
#include <ranges>
#include <span>
#include <string>
#include <string_view>

namespace test
{

// A test that needs a tag 28, a tag 29 or a head that is not in its preferred width writes its input as EDN
// embedded CBOR: cbor::parse_edn keeps the bytes of <<...>> as they are written, also the encoding indicators
// _0 to _3. A test holds no byte literal, so the library makes these bytes too.
inline std::expected<std::string, cbor::error> bytes_of_embedded_cbor(std::string_view const edn)
{
    auto const tree = cbor::parse_edn(edn);
    if (!tree) [[unlikely]]
        return std::unexpected(tree.error());
    return (*tree)->get<std::span<std::byte const>>().transform([](std::span<std::byte const> const bytes) {
        return std::string(std::from_range, bytes | std::views::transform([](std::byte const b) {
                                                return static_cast<char>(b);
                                            }));
    });
}

}
