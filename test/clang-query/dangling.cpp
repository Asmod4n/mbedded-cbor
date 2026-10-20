// Each form below binds a reference to the cbor::ref inside a
// temporary std::expected. The ref ends with the statement, so the
// reference reads dead memory, and no compiler warns. A cbor::result
// gives the value itself, so the library is safe in these forms
// (safe.cpp); a std::expected that the user makes is not.
// tools/ref.query must find every form.
#include <cbor/cbor.hpp>

#include <cstddef>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

template <class T>
std::expected<T, cbor::error> plain(cbor::result<T> r)
{
    return std::move(r);
}

std::size_t dangling(std::shared_ptr<std::string const> const &owner)
{
    auto const &by_star = *plain(cbor::at_path<"$.name", std::string_view>(owner));
    auto const &by_value = plain(cbor::at_path<"$.name", std::string_view>(owner)).value();
    return (*by_star).size() + (*by_value).size();
}

std::size_t dangling_member(cbor::lazy const &l)
{
    auto const &by_star = *plain(l.at_path<"$.name", std::string_view>());
    return (*by_star).size();
}

std::size_t dangling_expression(std::shared_ptr<std::string const> const &owner, cbor::lazy const &l)
{
    auto const e = cbor::jsonpath::compile<std::string_view>("$.name");
    auto const &by_star = *plain(cbor::at_path(owner, *e));
    auto const &by_value = plain(cbor::at_path(owner, *e)).value();
    auto const &member = *plain(l.at_path(*e));
    return (*by_star).size() + (*by_value).size() + (*member).size();
}
