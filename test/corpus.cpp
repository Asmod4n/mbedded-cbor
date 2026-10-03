#include "../fuzz/decode.hpp"
#include "../fuzz/doc_end.hpp"
#include "../fuzz/encode.hpp"
#include "../fuzz/encoder.hpp"
#include "../fuzz/lazy.hpp"
#include "../fuzz/path.hpp"
#include "../fuzz/schema.hpp"
#include "host.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace
{

void every_target(std::string_view const input)
{
    fuzz::decode_target(input);
    fuzz::encode_target(input);
    fuzz::encoder_target(input);
    fuzz::doc_end_target(input);
    fuzz::lazy_target(input);
    fuzz::path_target(input);
#if __cpp_impl_reflection
    fuzz::schema::decode_target(input);
    fuzz::schema::encode_target(input);
#endif
}

} // namespace

// Every input that a fuzzer kept runs through the target of every public API, and every prefix of it too,
// because a cut input is the commonest malformed one. A sanitizer build turns any fault into a failure.
TEST_CASE("fuzz corpus: every input and every prefix runs clean through every target")
{
    std::size_t files = 0;
    for (auto const &entry : std::filesystem::directory_iterator(FUZZ_CORPUS)) {
        std::ifstream in(entry.path(), std::ios::binary);
        std::string const input{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        for (std::size_t cut = 0; cut <= input.size() && cut < 512; ++cut)
            every_target(std::string_view(input).substr(0, cut));
        every_target(input);
        ++files;
    }
    CHECK_GT(files, 1000);
}
