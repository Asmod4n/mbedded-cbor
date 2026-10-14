// Each form below binds a reference to the cbor::owning_ref inside a
// temporary std::expected. The owning_ref ends with the statement, so the
// reference reads dead memory, and no compiler warns. tools/owning_ref.query
// must find both forms.
#include <cbor/cbor.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>

std::size_t dangling(std::shared_ptr<std::string const> const &owner)
{
    auto const &by_star = *cbor::at_path<"$.name", std::string_view>(owner);
    auto const &by_value = cbor::at_path<"$.name", std::string_view>(owner).value();
    return (*by_star).size() + (*by_value).size();
}

std::size_t dangling_member(cbor::lazy const &l)
{
    auto const &by_star = *l.at_path<"$.name", std::string_view>();
    return (*by_star).size();
}

std::size_t dangling_expression(std::shared_ptr<std::string const> const &owner, cbor::lazy const &l)
{
    auto const e = cbor::jsonpath::make_expression<std::string_view>("$.name");
    auto const &by_star = *cbor::at_path(owner, *e);
    auto const &by_value = cbor::at_path(owner, *e).value();
    auto const &member = *l.at_path(*e);
    return (*by_star).size() + (*by_value).size() + (*member).size();
}
