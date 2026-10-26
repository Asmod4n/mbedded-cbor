// The result is kept in a variable, so the ref lives as long as
// the reference. tools/ref.query must find nothing here.
#include <cbor/cbor.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

std::size_t safe(std::shared_ptr<std::string const> const &owner)
{
    auto const result = cbor::at_path<"$.name", std::string_view>(owner);
    auto const &name = *result;
    return (*name).size();
}

std::size_t safe_member(cbor::lazy const &l)
{
    auto const result = l.at_path<"$.name", std::string_view>();
    auto const &name = *result;
    return (*name).size();
}

std::size_t safe_expression(std::shared_ptr<std::string const> const &owner)
{
    auto const e = cbor::jsonpath::compile<std::string_view>("$.name");
    auto const result = cbor::at_path(owner, *e);
    auto const &name = *result;
    auto const one = cbor::jsonpath::compile<cbor::lazy>("$.name");
    auto const node = cbor::at_path(owner, *one);
    auto const all = cbor::jsonpath::compile<cbor::nodelist>("$.*");
    auto const nodes = cbor::at_path(owner, *all);
    return (*name).size() + nodes->size() + (node ? 1u : 0u);
}
