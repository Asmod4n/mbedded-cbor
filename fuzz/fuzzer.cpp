#include "one_input.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size)
{
    fuzz::one_input(std::string_view(reinterpret_cast<char const *>(data), size));
    return 0;
}
