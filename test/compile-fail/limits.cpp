// Each limit is a constant that the build sets. A value above the hard bound of its limit must not compile,
// so the tests that build this file set one macro above its bound and expect the build to fail with the
// message of the check.
#include <cbor/cbor.hpp>

int main()
{
    return cbor::limits::nesting_depth == 0 ? 1 : 0;
}
