// The accessor of a list is a range only where the list holds records or fixed-size structs, and only as an lvalue.
// An rvalue accessor ends before its iterators do, so begin, cbegin and rbegin on it do not compile. A list of
// texts, of optional structs or of lists has no iterator of elements. CBOR_TEST_CASE selects the case: 1 a list of
// texts, 2 a list of optional structs, 3 a list of lists, 4 begin on an rvalue, 5 cbegin on an rvalue, 6 rbegin on an
// rvalue, 7 the right uses on lvalues, which compile.
#include <cbor/cbor.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

struct article {
    std::string name;
    std::uint16_t count;
};

struct spot {
    std::uint16_t x;
    std::uint16_t y;
};

struct shelf {
    std::vector<article> articles;
    std::vector<spot> spots;
    std::vector<std::string> labels;
    std::vector<std::optional<article>> maybe;
    std::vector<std::vector<article>> rows;
};

template <>
struct cbor::describe_members<article> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1797};
    static constexpr std::tuple members{
        CBOR_MEMBER(article, name),
        CBOR_MEMBER(article, count),
    };
};

template <>
struct cbor::describe_members<spot> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1798};
    static constexpr std::tuple members{
        CBOR_MEMBER(spot, x),
        CBOR_MEMBER(spot, y),
    };
};

template <>
struct cbor::describe_members<shelf> {
    static constexpr std::optional<cbor::tag> tag = cbor::tag{1799};
    static constexpr std::tuple members{
        CBOR_MEMBER(shelf, articles),
        CBOR_MEMBER(shelf, spots),
        CBOR_MEMBER(shelf, labels),
        CBOR_MEMBER(shelf, maybe),
        CBOR_MEMBER(shelf, rows),
    };
};

std::size_t use(cbor::schema<shelf>::accessor<> const &root)
{
#if CBOR_TEST_CASE == 1
    auto const list = *root.at_path<"$.labels">();
    return static_cast<std::size_t>(list.begin() - list.end());
#elif CBOR_TEST_CASE == 2
    auto const list = *root.at_path<"$.maybe">();
    return static_cast<std::size_t>(list.begin() - list.end());
#elif CBOR_TEST_CASE == 3
    auto const list = *root.at_path<"$.rows">();
    return static_cast<std::size_t>(list.begin() - list.end());
#elif CBOR_TEST_CASE == 4
    auto const list = root.at_path<"$.articles">();
    auto const first = std::move(*list).begin();
    return static_cast<std::size_t>(list->end() - first);
#elif CBOR_TEST_CASE == 5
    auto const list = root.at_path<"$.articles">();
    auto const first = std::move(*list).cbegin();
    return static_cast<std::size_t>(list->end() - first);
#elif CBOR_TEST_CASE == 6
    auto const list = root.at_path<"$.articles">();
    auto const first = std::move(*list).rbegin();
    return static_cast<std::size_t>(first - list->rend());
#else
    auto const articles = *root.at_path<"$.articles">();
    auto const spots = *root.at_path<"$.spots">();
    return static_cast<std::size_t>((articles.end() - articles.cbegin()) + (spots.rend() - spots.rbegin()));
#endif
}
