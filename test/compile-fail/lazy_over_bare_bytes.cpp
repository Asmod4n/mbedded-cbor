// A lazy over a bare string_view would read the buffer of the caller with no owner. This file must not compile;
// the test that builds it expects the build to fail.
#include <cbor/cbor.hpp>

#include <string_view>

int main()
{
    std::string_view const encoded{"\xa1\x61\x61\x01"};
    auto const e = cbor::jsonpath::make_expression<cbor::lazy>("$.a");
    return cbor::at_path(encoded, *e).has_value() ? 0 : 1;
}
