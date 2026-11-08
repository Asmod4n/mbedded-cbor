#pragma once

#include "decode.hpp"
#include "value.hpp"
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>

#include <expected>
#include <string_view>
#include <vector>

namespace test
{

// The value of the node that a singular query selects.
inline std::expected<value, error> at_path(std::string_view const path, cbor::lazy const &l)
{
    return cbor::path::compile<cbor::lazy>(path).and_then(
        [&l](cbor::path::query<cbor::lazy> const &e) { return value_of(l.at_path(e)); });
}

template <cbor::fixed_string Path>
std::expected<value, error> at_path(cbor::lazy const &l)
{
    return value_of(l.at_path<Path>());
}

// The nodelist of a query as an array of the values of its nodes.
inline std::expected<value, error> values_of(std::expected<std::vector<cbor::lazy>, error> const &nodes)
{
    if (!nodes)
        return std::unexpected(nodes.error());
    array a;
    for (cbor::lazy const &n : *nodes) {
        auto v = value_of(n);
        if (!v)
            return std::unexpected(v.error());
        a.push_back(std::move(*v));
    }
    return value{std::move(a)};
}

inline std::expected<value, error> query(std::string_view const path, cbor::lazy const &l)
{
    return cbor::path::compile<cbor::nodelist>(path).and_then(
        [&l](cbor::path::query<cbor::nodelist> const &e) { return values_of(l.at_path(e)); });
}

template <cbor::fixed_string Path>
std::expected<value, error> query(cbor::lazy const &l)
{
    return values_of(l.at_path<Path, cbor::nodelist>());
}

} // namespace test
