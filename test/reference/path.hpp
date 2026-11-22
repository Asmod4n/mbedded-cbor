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
inline std::expected<value, error> read_value_at_path(std::string_view const path, cbor::lazy const &l)
{
    auto const e = cbor::path::compile<cbor::lazy>(path);
    if (!e)
        return std::unexpected(e.error());
    auto const node = l.at_path(*e);
    return make_value_from_item(node);
}

template <cbor::fixed_string Path>
std::expected<value, error> read_value_at_path(cbor::lazy const &l)
{
    auto const node = l.at_path<Path>();
    return make_value_from_item(node);
}

// The nodelist of a query as an array of the values of its nodes.
inline std::expected<value, error> make_values_from_nodelist(cbor::result<cbor::nodelist> const &nodes)
{
    if (!nodes)
        return std::unexpected(nodes.error());
    array a;
    for (cbor::lazy const &n : *nodes) {
        auto v = make_value_from_item(n);
        if (!v)
            return std::unexpected(v.error());
        a.push_back(std::move(*v));
    }
    return value{std::move(a)};
}

inline std::expected<value, error> query(std::string_view const path, cbor::lazy const &l)
{
    auto const e = cbor::path::compile<cbor::nodelist>(path);
    if (!e)
        return std::unexpected(e.error());
    auto const nodes = l.at_path(*e);
    return make_values_from_nodelist(nodes);
}

template <cbor::fixed_string Path>
std::expected<value, error> query(cbor::lazy const &l)
{
    auto const nodes = l.at_path<Path, cbor::nodelist>();
    return make_values_from_nodelist(nodes);
}

} // namespace test
