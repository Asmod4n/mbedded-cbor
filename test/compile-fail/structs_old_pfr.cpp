// Without reflection, cbor::structs reads members with boost::pfr::get_name, which Boost.PFR has since 1.84. An older
// Boost.PFR on the include path must stop the build. The message must name the version. It must not be an error deep
// inside a template.
#include <cbor/cbor.hpp>

int main()
{
}
