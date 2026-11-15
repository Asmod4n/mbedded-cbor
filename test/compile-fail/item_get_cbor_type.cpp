// get takes a C++ type. A member of cbor::type is a value, not a type, so get<cbor::type::array> has no matching
// function. This file must not compile; the test that builds it expects the build to fail.
#include <cbor/cbor.hpp>

#include <string_view>

int main()
{
    auto const tree = cbor::decode(std::string_view("\x80"));
    return (*tree)->get<cbor::type::array>().has_value() ? 0 : 1;
}
