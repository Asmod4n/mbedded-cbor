// The result is kept in a variable, so the ref lives as long as
// the reference. A reference bound to *r or r.value() of a temporary
// cbor::result binds the value itself and extends its life.
// tools/ref.query must find nothing here.
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

std::size_t safe_temporary(std::shared_ptr<std::string const> const &owner, cbor::lazy const &l)
{
    auto const &by_star = *cbor::at_path<"$.name", std::string_view>(owner);
    auto const &by_value = cbor::at_path<"$.name", std::string_view>(owner).value();
    auto const &member = *l.at_path<"$.name", std::string_view>();
    return (*by_star).size() + (*by_value).size() + (*member).size();
}
