// A program that uses the library the way a project outside this tree
// does: it includes the header and reads one value. The tests of
// test/cmake build it after an install and after add_subdirectory, so
// that a fault in the install rules or the exported target fails a test.
#include <cbor/cbor.hpp>
#include <string>

int main()
{
    // {"id": 42}
    std::string const bytes = "\xa1\x62id\x18\x2a";
    auto const id = cbor::at_path<"$.id", int>(bytes);
    return id && *id == 42 ? 0 : 1;
}
