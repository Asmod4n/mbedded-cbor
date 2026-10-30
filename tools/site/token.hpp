#pragma once

#include <cbor/cbor.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace site
{

struct token {
    std::uint8_t kind;
    std::uint32_t offset;
    std::uint32_t length;
};

struct token_file {
    std::string path;
    std::string text;
    std::vector<token> tokens;
};

struct line_range {
    std::string path;
    std::uint32_t first;
    std::uint32_t last;
};

} // namespace site

CBOR_DEFINE_TYPE(site::token, cbor::tag{1970}, CBOR_MEMBER(site::token, kind), CBOR_MEMBER(site::token, offset),
                 CBOR_MEMBER(site::token, length));
CBOR_DEFINE_TYPE(site::token_file, cbor::tag{1971}, CBOR_MEMBER(site::token_file, path),
                 CBOR_MEMBER(site::token_file, text), CBOR_MEMBER(site::token_file, tokens));
