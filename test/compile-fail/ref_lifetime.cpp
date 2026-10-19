// Owner decision 2026-10-17: what the compiler can check about the lifetime of a view is checked. Each case reads a
// view after the ref or the owning tree that keeps its bytes alive is gone; clang with -Wlifetime-safety refuses each
// one, and every compiler refuses a read through a temporary. Case 0 is the safe use and must compile.
#include <cbor/cbor.hpp>

#include <memory>
#include <string>
#include <string_view>

std::size_t use(std::string_view const v)
{
    return v.size();
}

cbor::ref<std::string_view> name()
{
    return *cbor::at_path<"$.a", std::string_view>(std::make_shared<std::string const>("\xa1\x61\x61\x61x"));
}

#if CBOR_TEST_CASE == 0
std::size_t read()
{
    auto const r = name();
    return use(*r);
}
#elif CBOR_TEST_CASE == 1
std::size_t read()
{
    std::string_view v;
    {
        auto const r = name();
        v = *r;
    }
    return use(v);
}
#elif CBOR_TEST_CASE == 2
std::string_view read()
{
    auto const r = name();
    return *r;
}
#elif CBOR_TEST_CASE == 3
std::size_t read()
{
    return use(*name());
}
#elif CBOR_TEST_CASE == 4
std::size_t read()
{
    cbor::item const *root;
    {
        auto const tree = cbor::decode("\x81\x01").value();
        root = &*tree;
    }
    return root->get<cbor::type::array>().value().size();
}
#endif

int main()
{
    return static_cast<int>(read() == 0);
}
