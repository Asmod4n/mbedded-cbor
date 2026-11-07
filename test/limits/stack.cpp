#include "../owned_copy.hpp"
#include "../threads.hpp"
#include <cbor/decode.hpp>
#include <cbor/diagnostic_notation.hpp>
#include <cbor/error.hpp>
#include <cbor/item_size.hpp>
#include <cbor/lazy.hpp>
#include <cbor/query.hpp>
#include <cbor/schema.hpp>
#include <cbor/structs.hpp>

#include <doctest/doctest.h>

#include <cstddef>
#include <expected>
#include <functional>
#include <optional>
#include <string>
#include <cstdint>
#include <utility>
#include <variant>
#include <vector>

#if defined(_WIN64)
#include <process.h>
#elif __has_include(<pthread.h>)
#include <pthread.h>
#endif

using namespace std::string_literals;
using cbor::error;

namespace limits_stack
{
namespace
{

struct node {
    std::vector<node> c;
};

struct twig {
    std::uint8_t n;
};

struct leaf : twig {
};

struct holder;

struct limb : twig {
    std::vector<holder> next;
};

// A type that holds itself through a variant: cbor::schema writes and reads it by recursion.
struct holder {
    std::variant<leaf, limb> held;
};

} // namespace
} // namespace limits_stack

CBOR_DEFINE_DERIVED_TYPE_MEMBERS(limits_stack::leaf, limits_stack::twig, cbor::tag{1525}, n);
CBOR_DEFINE_DERIVED_TYPE_MEMBERS(limits_stack::limb, limits_stack::twig, cbor::tag{1526}, n, next);
CBOR_DEFINE_TYPE_MEMBERS(limits_stack::holder, cbor::tag{1527}, held);

namespace limits_stack
{
namespace
{

// A chain of holders: each limb holds a list of one holder, and the last holder holds a leaf. A chain of k links is
// 2 k + 1 levels deep.
holder holder_chain(std::size_t const links)
{
    holder h{leaf{}};
    for (std::size_t i = 0; i < links; ++i) {
        limb f{};
        f.next.push_back(std::move(h));
        h = holder{std::move(f)};
    }
    return h;
}

// The build sets the nesting depth to its hard bound, 512. A message of 511 nested arrays around 1 has its innermost
// item at depth 511, inside the bound (RFC 8949 3: each array is one level).
constexpr std::size_t levels = 511;

std::string arrays_around_one(std::size_t const n)
{
    return std::string(n, '\x81') + "\x01"s;
}

// On a stack too small for the bound, a reader stops with nesting_depth_exceeded where the stack ends, and gives no
// other error and no crash.
template <class R>
void reads(R const &r)
{
    if (!r)
        CHECK_EQ(r.error(), error::nesting_depth_exceeded);
}

std::expected<std::vector<cbor::lazy>, error> query(std::string const &message, std::string const &path)
{
    auto const l = test::lazy_of_copy(message);
    auto const q = cbor::jsonpath::compile<cbor::nodelist>(path);
    CHECK(l.has_value());
    CHECK(q.has_value());
    if (!l || !q)
        return std::unexpected(error::syntax_error);
    return l->at_path(*q);
}

// The chain and the message that decode reads stay alive outside the thread: the destructor of a chain recurses
// through its links, and the test shrinks the stack of the reader, not of whoever ends the value.
struct chain_values {
    holder chain;
    std::optional<std::expected<cbor::ref<holder>, error>> decoded;
};

void every_reader(chain_values &kept)
{
    std::string const deep = arrays_around_one(levels);
    reads(cbor::decode(std::string(deep)));
    auto const l = test::lazy_of_copy(deep);
    CHECK(l.has_value());
    if (l)
        reads(l->clone());
    reads(cbor::diagnostic_notation(deep));
    reads(cbor::item_size(deep));
    reads(cbor::jsonpath::compile<cbor::lazy>("$[" + std::string(levels, '[') + "1" + std::string(levels, ']') + "]"));
    reads(query(deep, "$..*"));
    reads(query(deep, "$..[?@ == 1]"));
    reads(query("\x82"s + arrays_around_one(levels - 1) + arrays_around_one(levels - 1), "$[?@ == $[1]]"));
    // A struct that holds a std::vector of itself is a map that holds an array (RFC 8949 3.2): two levels for
    // each step; 255 steps put the innermost empty array at depth 511.
    std::string message;
    for (std::size_t i = 0; i < (levels - 1) / 2; ++i)
        message += "\xa1\x61" "c\x81"s;
    message += "\xa1\x61" "c\x80"s;
    auto const decoded = cbor::structs<node>::decode(std::move(message));
    reads(decoded);
    if (decoded)
        reads(cbor::structs<node>::encode(**decoded));
    auto const written = cbor::schema<holder>::encode(kept.chain);
    reads(written);
    if (written) {
        kept.decoded = cbor::schema<holder>::decode(std::string(*written));
        reads(*kept.decoded);
    }
}

#if defined(_WIN64)
// The flag of _beginthreadex that makes the size the reservation of the stack, not its first commit.
constexpr unsigned stack_size_param_is_a_reservation = 0x00010000;

unsigned __stdcall run(void *const work)
{
    (*static_cast<std::function<void()> *>(work))();
    return 0;
}

// The test makes the stack of a thread smaller than the platform gives (owner: a test may shrink a stack to test the
// stack bound); _beginthreadex takes its size.
void on_stack_of(std::size_t const bytes, std::function<void()> work)
{
    auto const thread = _beginthreadex(nullptr, static_cast<unsigned>(bytes), run, &work,
                                       stack_size_param_is_a_reservation, nullptr);
    REQUIRE(thread != 0);
    REQUIRE(_cwait(nullptr, static_cast<std::intptr_t>(thread), 0) != -1);
}
#elif __has_include(<pthread.h>)
void *run(void *const work)
{
    (*static_cast<std::function<void()> *>(work))();
    return nullptr;
}

// The test makes the stack of a thread smaller than the platform gives (owner: a test may shrink a stack to test the
// stack bound); std::thread takes no stack size, the attributes of POSIX threads do.
void on_stack_of(std::size_t const bytes, std::function<void()> work)
{
    pthread_attr_t attributes;
    REQUIRE_EQ(pthread_attr_init(&attributes), 0);
    REQUIRE_EQ(pthread_attr_setstacksize(&attributes, bytes), 0);
    pthread_t thread;
    REQUIRE_EQ(pthread_create(&thread, &attributes, run, &work), 0);
    REQUIRE_EQ(pthread_join(thread, nullptr), 0);
    pthread_attr_destroy(&attributes);
}
#endif

} // namespace
} // namespace limits_stack

using namespace limits_stack;

// Every reader that walks nested items reads the stack bound of its thread and stops before the stack ends, with
// the error of the nesting depth. The main thread reads a message at the bound with what the platform gives it.
TEST_CASE("stack: every recursive reader on the main thread")
{
    chain_values kept{holder_chain((levels - 1) / 2), std::nullopt};
    every_reader(kept);
}

#if defined(_WIN64) || __has_include(<pthread.h>)
// Threads with stacks of 64 KiB and 256 KiB hold fewer levels than the bound: each reader stops with
// nesting_depth_exceeded there and does not run past the stack.
TEST_CASE("stack: every recursive reader on a thread with a small stack" * doctest::skip(!test::threads_available))
{
    for (std::size_t const kib : {64uz, 256uz}) {
        CAPTURE(kib);
        chain_values kept{holder_chain((levels - 1) / 2), std::nullopt};
        on_stack_of(kib * 1024, [&kept] { every_reader(kept); });
    }
}
#endif
