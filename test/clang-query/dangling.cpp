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
    auto const &by_star = *cbor::at_path<"$.name", std::string_view>(owner, *owner);
    auto const &by_value = cbor::at_path<"$.name", std::string_view>(owner, *owner).value();
    return (*by_star).size() + (*by_value).size();
}
