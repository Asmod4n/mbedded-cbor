// get gives a view into the tree and copies nothing, so std::string is not a type that get takes. This file must not
// compile; the test that builds it expects the build to fail.
#include <cbor/cbor.hpp>

#include <string>
#include <string_view>

int main()
{
    auto const tree = cbor::decode(std::string_view("\x61\x61"));
    return (*tree)->get<std::string>().has_value() ? 0 : 1;
}
