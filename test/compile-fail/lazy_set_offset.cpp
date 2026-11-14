// The position of a lazy is state of the lazy, and a program cannot set it. This file must not compile;
// the test that builds it expects the build to fail.
#include <cbor/cbor.hpp>

#include <memory>
#include <string>

int main()
{
    auto l = cbor::lazy::view(std::make_shared<std::string const>("\x82\x01\x02"));
    l->offset = 1;
    return 0;
}
