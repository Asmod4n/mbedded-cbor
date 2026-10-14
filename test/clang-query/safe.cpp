// The result is kept in a variable, so the owning_ref lives as long as
// the reference. tools/owning_ref.query must find nothing here.
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
