// get<K> with a K that is no member of cbor::type has no alternative type. This file must not compile; the test
// that builds it expects the build to fail.
#include <cbor/cbor.hpp>

#include <string_view>

int main()
{
    auto const tree = cbor::decode(std::string_view("\x01"));
    return (*tree)->get<static_cast<cbor::type>(200)>().has_value() ? 0 : 1;
}
