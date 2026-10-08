#include "binding.hpp"

#include <array>
#include <cstddef>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

using cbor::error;
using cbor::validity;

// The owner set the bound of DepthMax to 1024 and its default to 128. Every form that takes DepthMax reads both
// from these two constants, so this test is the one place that holds the two numbers.
TEST_CASE("validity: the limit and the default of the nesting depth")
{
    CHECK_EQ(validity::nesting_depth_limit, 1024u);
    CHECK_EQ(validity::nesting_depth_default, 128u);
}

// One function decides whether a depth is allowed, at compile time for DepthMax and at run time for the depth of an
// item. A depth up to the maximum is allowed and every depth above it is refused, for every depth up to one past
// the limit.
TEST_CASE("validity: check_nesting_depth for every depth up to one past the limit")
{
    for (std::size_t depth = 0; depth <= 1025; ++depth) {
        auto const r = validity::check_nesting_depth(depth, 1024);
        if (depth <= 1024)
            CHECK(r.has_value());
        else
            CHECK_EQ(r.error(), error::nesting_depth_exceeded);
    }
    constexpr bool limit_allowed = validity::check_nesting_depth(1024, validity::nesting_depth_limit).has_value();
    constexpr bool past_limit_allowed = validity::check_nesting_depth(1025, validity::nesting_depth_limit).has_value();
    CHECK(limit_allowed);
    CHECK_FALSE(past_limit_allowed);
}

namespace
{

struct shared_ptr_case {
    std::shared_ptr<int const> pointer;
    bool empty;
    bool null;
};

std::shared_ptr<int const> const holder = std::make_shared<int const>(7);
int const elsewhere = 8;

// The four states of a std::shared_ptr after [util.smartptr.shared]: it owns an object or not (empty means it owns
// none), and it stores a pointer or not (null). Each state is built with a constructor of the standard.
shared_ptr_case const shared_ptr_cases[] = {
    {std::shared_ptr<int const>{}, true, true},
    {std::shared_ptr<int const>(std::shared_ptr<int const>{}, &elsewhere), true, false},
    {std::shared_ptr<int const>(holder, nullptr), false, true},
    {holder, false, false},
};

} // namespace

// An owner that owns no object keeps nothing alive, whatever pointer it stores. The check must throw for exactly the
// two empty states, so that a view beside such an owner is never handed out.
TEST_CASE("validity: throw_logic_error_if_empty for every state of a shared_ptr")
{
    for (shared_ptr_case const &c : shared_ptr_cases) {
        if (c.empty)
            CHECK_THROWS_AS(validity::throw_logic_error_if_empty(c.pointer, "empty"), std::logic_error);
        else
            CHECK_NOTHROW(validity::throw_logic_error_if_empty(c.pointer, "empty"));
    }
}

// A pointer that is read through must not be null, whether it owns an object or not. The check must throw for
// exactly the two null states.
TEST_CASE("validity: throw_logic_error_if_null for every state of a shared_ptr")
{
    for (shared_ptr_case const &c : shared_ptr_cases) {
        if (c.null)
            CHECK_THROWS_AS(validity::throw_logic_error_if_null(c.pointer, "null"), std::logic_error);
        else
            CHECK_NOTHROW(validity::throw_logic_error_if_null(c.pointer, "null"));
    }
}

namespace
{

constexpr std::size_t size_max = std::numeric_limits<std::size_t>::max();

// Factors around each width a product can have: zero, one, small numbers, the borders of 32 bits, the halves and
// the top of std::size_t.
constexpr std::array<std::size_t, 13> factors{0,
                                              1,
                                              2,
                                              3,
                                              std::size_t{0xffffffff},
                                              std::size_t{0xffffffff} + 1,
                                              std::size_t{0xffffffff} + 2,
                                              size_max / 3,
                                              size_max / 2,
                                              size_max / 2 + 1,
                                              size_max - 1,
                                              size_max,
                                              std::size_t{1} << (std::numeric_limits<std::size_t>::digits / 2)};

constexpr auto products_at_compile_time = [] {
    std::array<std::array<bool, factors.size()>, factors.size()> fits{};
    for (std::size_t i = 0; i < factors.size(); ++i)
        for (std::size_t j = 0; j < factors.size(); ++j)
            fits[i][j] = cbor::validity::checked_mul(factors[i], factors[j]).has_value();
    return fits;
}();

} // namespace

// A map or a set of members is valid only when no two of its keys are equal. The check reads a sorted range, so it
// must agree with a comparison of every pair, for every sorted sequence of up to four keys from three values, and
// through a projection to the key of a pair.
TEST_CASE("validity: keys_unique for every sorted sequence of up to four keys")
{
    std::vector<std::vector<int>> sequences{{}};
    for (std::size_t length = 1; length <= 4; ++length) {
        std::vector<std::vector<int>> longer;
        for (std::vector<int> const &s : sequences)
            if (s.size() == length - 1)
                for (int key = s.empty() ? 0 : s.back(); key <= 2; ++key) {
                    std::vector<int> next = s;
                    next.push_back(key);
                    longer.push_back(next);
                }
        sequences.insert(sequences.end(), longer.begin(), longer.end());
    }
    CHECK_EQ(sequences.size(), 35u);
    for (std::vector<int> const &s : sequences) {
        bool distinct = true;
        for (std::size_t i = 0; i < s.size(); ++i)
            for (std::size_t j = i + 1; j < s.size(); ++j)
                distinct = distinct && s[i] != s[j];
        CHECK_EQ(validity::keys_unique(s), distinct);
        std::vector<std::pair<int, std::string>> pairs;
        for (int const key : s)
            pairs.emplace_back(key, std::to_string(pairs.size()));
        CHECK_EQ(validity::keys_unique(pairs, &std::pair<int, std::string>::first), distinct);
    }
}

#ifdef __SIZEOF_INT128__
// C23 ckd_mul gives the product when it is representable and reports an overflow otherwise. The compiler and the run
// time use the same function, so both must give the answer of the exact product, which a wider type computes here.
TEST_CASE("validity: checked_mul for every pair of factors, at compile time and at run time")
{
    for (std::size_t i = 0; i < factors.size(); ++i)
        for (std::size_t j = 0; j < factors.size(); ++j) {
            std::size_t const a = factors[i];
            std::size_t const b = factors[j];
            cbor::uint128 const exact = static_cast<cbor::uint128>(a) * b;
            bool const fits = exact <= size_max;
            auto const product = validity::checked_mul(a, b);
            CHECK_EQ(product.has_value(), fits);
            CHECK_EQ(products_at_compile_time[i][j], fits);
            if (fits)
                CHECK_EQ(*product, a * b);
            else
                CHECK_EQ(product.error(), std::errc::value_too_large);
        }
}
#endif
