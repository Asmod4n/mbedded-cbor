// The library reads GNU attributes and builtins that the frontend of MSVC does not take; on Windows it builds with
// clang-cl. The library must refuse the frontend of MSVC with a message that names clang-cl, not fail later in its
// own code. The test defines _MSC_VER as that frontend does, in a g++ build, which defines no __clang__.
#include <cbor/cbor.hpp>

int main()
{
}
