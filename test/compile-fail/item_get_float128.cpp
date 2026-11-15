// A tree holds no element of a typed array as a node, and only such an element is a 128-bit float, so get does not
// take std::float128_t. Where the compiler has no std::float128_t, long double stands for it: get does not take it
// either. This file must not compile; the test that builds it expects the build to fail.
#include <cbor/cbor.hpp>

#include <string_view>

#if __has_include(<stdfloat>)
#include <stdfloat>
#endif

#ifdef __STDCPP_FLOAT128_T__
using wide_float = std::float128_t;
#else
using wide_float = long double;
#endif

int main()
{
    auto const tree = cbor::decode(std::string_view("\xf9\x3c\x00"));
    return (*tree)->get<wide_float>().has_value() ? 0 : 1;
}
