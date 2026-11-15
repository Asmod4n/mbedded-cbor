// cbor::lazy::at takes an index and reads an array. A text is a map key, and cbor::lazy::at_key reads it (the form of
// simdjson). Each case of this file must not compile; the test that builds it expects the build to fail.
#include <cbor/lazy.hpp>

#include <string_view>

int main()
{
    cbor::lazy const l{};
#if CBOR_TEST_CASE == 1
    return l.at("a").has_value() ? 1 : 0;
#elif CBOR_TEST_CASE == 2
    return l.at(std::string_view{"a"}).has_value() ? 1 : 0;
#endif
}
