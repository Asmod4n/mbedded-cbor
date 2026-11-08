// cbor::key has no default constructor, and its text comes only from a literal at compile time (owner 2026-10-09).
// A key with no text and no number, or with a text that is known only at run time, would name no member and could
// point at a text that does not live. Each case of this file must not compile; the test that builds it expects the
// build to fail.
#include <cbor/rfc9535.hpp>

int main(int const argc, char const *const argv[])
{
#if CBOR_TEST_CASE == 1
    cbor::key const k{};
    return argc + static_cast<int>(k.number) + (argv == nullptr ? 1 : 0);
#elif CBOR_TEST_CASE == 2
    cbor::key const k{argv[argc - 1]};
    return k.text == nullptr ? 1 : 0;
#endif
}
