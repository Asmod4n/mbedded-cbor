#pragma once

#include "../test/host.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace fuzz
{

inline constexpr std::array<std::string_view, 12> paths{
    "$",       "$.a",         "$.a.b.c",          "$[0]",          "$[-1]", "$[*]", "$.a[*]", "$.items[*].id",
    "$[*][*]", "$.a[0][*].b", "$[\"weird key\"]", "$.a[*].b[*].c",
};

// The same channels as the fuzzer of mruby-cbor: decode, encode and decode again; the same with
// shared references; doc_end; lazy access and every path of the list; the raw input as a path.
inline void one_input(std::string_view const input)
{
    test_host host;
    auto const value = cbor::decode<16>(host, input);
    if (value) {
        string_writer w;
        if (cbor::encode<16>(host, w, *value))
            (void)cbor::decode<16>(host, w.bytes);
    }
    (void)cbor::doc_end<16>(input);
    cbor::lazy const root{input, 0};
    (void)cbor::lazy_decode<16>(host, root);
    (void)cbor::lazy_at<16>(root, std::int64_t{0});
    (void)cbor::lazy_at<16>(root, std::int64_t{-1});
    (void)cbor::lazy_at<16>(root, "a");
    for (std::string_view const p : paths) {
        auto const steps = cbor::path_compile(p);
        if (steps)
            (void)cbor::path_decode<16>(host, *steps, root);
    }
    (void)cbor::path_compile(input);
}

} // namespace fuzz
