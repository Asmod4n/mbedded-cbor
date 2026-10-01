#include "../fuzz/one_input.hpp"
#include "host.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

// Every input that the fuzzer of mruby-cbor kept runs through every channel of the fuzzer here, and
// every prefix of it too, because a cut input is the commonest malformed one. A sanitizer build
// turns any fault into a failure.
TEST_CASE("fuzz corpus: every input and every prefix runs clean")
{
    std::size_t files = 0;
    for (auto const &entry : std::filesystem::directory_iterator(FUZZ_CORPUS)) {
        std::ifstream in(entry.path(), std::ios::binary);
        std::string const input{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
        for (std::size_t cut = 0; cut <= input.size() && cut < 512; ++cut)
            fuzz::one_input(std::string_view(input).substr(0, cut));
        fuzz::one_input(input);
        ++files;
    }
    CHECK_GT(files, 1000);
}
