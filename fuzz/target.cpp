#include "decode.hpp"
#include "item_end.hpp"
#include "encode.hpp"
#include "encoder.hpp"
#include "lazy.hpp"
#include "path.hpp"
#include "schema.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size)
{
    FUZZ_TARGET(std::string_view(reinterpret_cast<char const *>(data), size));
    return 0;
}
