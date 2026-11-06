// A sequence::iterator comes from sequence::begin only. An iterator made by hand could name a size that its bytes
// do not have, so its constructor is private and this does not compile.
#include <cbor/cbor.hpp>

#include <cstddef>
#include <string_view>

int main()
{
    std::string_view const bytes = "\x01";
    cbor::sequence::iterator const it{bytes, std::size_t{1}};
    return (*it).has_value() ? 0 : 1;
}
