#include <cbor/sequence.hpp>

int main()
{
    auto const owner = std::make_shared<std::string const>("\x01\x02\x03", 3);
    std::size_t count = 0;
    for (auto const item : cbor::sequence(owner)) {
        if (!item)
            return 1;
        count += item->size();
    }
    return count == 3 ? 0 : 1;
}
