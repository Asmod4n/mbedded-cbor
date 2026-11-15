// visit returns what f returns, so f must give one type for every kind of node, as std::visit requires. This f gives
// int for an unsigned integer and double for every other kind. This file must not compile; the test that builds it
// expects the build to fail. Where cbor::int128 does not exist, visit does not exist either.
#include <cbor/cbor.hpp>

#include <cstdint>
#include <string_view>
#include <type_traits>

int main()
{
    auto const tree = cbor::decode(std::string_view("\x01"));
    auto const r = (*tree)->visit([](auto const v) {
        if constexpr (std::is_same_v<decltype(v), std::uint64_t const>)
            return 1;
        else
            return 2.0;
    });
    return static_cast<int>(r);
}
