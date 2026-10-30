#include "decode.hpp"
#include "diagnostic.hpp"
#include "item_size.hpp"
#include "encode.hpp"
#include "encoder.hpp"
#include "lazy.hpp"
#include "path.hpp"
#include "sequence_reader.hpp"
#include "schema.hpp"
#include "structs.hpp"

#include <cstddef>
#include <cstdint>
#include <string_view>

extern "C" int LLVMFuzzerTestOneInput(std::uint8_t const *data, std::size_t size)
{
    FUZZ_TARGET(std::string_view(reinterpret_cast<char const *>(data), size));
    return 0;
}
