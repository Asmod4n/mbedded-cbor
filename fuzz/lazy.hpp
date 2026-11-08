// Gives the fuzz target of cbor::lazy.
#pragma once

#include "common.hpp"
#include "../test/owned_copy.hpp"

#include <cbor/item_size.hpp>
#include <cbor/lazy.hpp>

#include <bit>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace fuzz
{

inline void lazy_channels(std::string_view const input)
{
    auto const eager = test::decoded(input);
    (void)cbor::item_size(input);
    auto const decoded = test::lazy_of_copy(std::string{input});
    if (!decoded)
        return;
    cbor::lazy const &root = *decoded;
    auto const whole = test::value_of(root);
    if (eager && whole)
        require(same(*eager, *whole));
    if (whole) {
        string_writer w;
        if (test::encode(w, *whole)) {
            auto const again = test::decoded(w.encoded);
            require(again.has_value() && same_number(*whole, *again));
        }
    }
    if (eager) {
        if (auto const *a = test::get_if<test::array>(*eager)) {
            if (auto const elements = root.elements()) {
                std::size_t i = 0;
                for (auto const e : *elements) {
                    if (!e || i >= a->size())
                        break;
                    if (auto const d = test::value_of(*e))
                        require(same(*d, a->at(i)));
                    if (auto const at = root.at(i))
                        if (auto const d = test::value_of(*at))
                            require(same(*d, a->at(i)));
                    ++i;
                }
            }
        }
        if (auto const *m = test::get_if<test::map>(*eager)) {
            if (auto const entries = root.entries()) {
                std::size_t i = 0;
                for (auto const e : *entries) {
                    if (!e || i >= m->size())
                        break;
                    if (auto const k = test::value_of(e->first))
                        require(same(*k, m->at(i).key));
                    if (auto const v = test::value_of(e->second))
                        require(same(*v, m->at(i).val));
                    ++i;
                }
            }
        }
    }
    std::string_view const key = input.substr(0, 4);
    (void)root.at(0);
    (void)root.at(cbor::key{0});
    (void)root.at(cbor::key{-1});
    (void)root.at("a");
    if (auto const a = root.at(key))
        if (auto const b = a->at(0))
            (void)b->at(key);
}

// Two results of the same call on two forms of the same input: the same value or the same error code.
template <class R>
void require_same_result(R const &a, R const &b)
{
    require(a.has_value() == b.has_value());
    if (!a) {
        require(a.error() == b.error());
        require_error_value(a.error());
        return;
    }
    if constexpr (requires { **a == **b; })
        require(**a == **b);
    else if constexpr (std::is_same_v<std::remove_cvref_t<decltype(*a)>, double>)
        require(std::bit_cast<std::uint64_t>(*a) == std::bit_cast<std::uint64_t>(*b));
    else if constexpr (requires { *a == *b; })
        require(*a == *b);
}

// The reads of one node that take no argument from the input give the same result on every form of the input.
inline void node_reads_compare(cbor::lazy const &a, cbor::lazy const &b)
{
    require_same_result(a.type(), b.type());
    require_same_result(a.empty(), b.empty());
    require_same_result(a.size(), b.size());
    require_same_result(a.get<std::int8_t>(), b.get<std::int8_t>());
    require_same_result(a.get<std::int16_t>(), b.get<std::int16_t>());
    require_same_result(a.get<std::int32_t>(), b.get<std::int32_t>());
    require_same_result(a.get<std::int64_t>(), b.get<std::int64_t>());
    require_same_result(a.get<std::uint8_t>(), b.get<std::uint8_t>());
    require_same_result(a.get<std::uint16_t>(), b.get<std::uint16_t>());
    require_same_result(a.get<std::uint32_t>(), b.get<std::uint32_t>());
    require_same_result(a.get<std::uint64_t>(), b.get<std::uint64_t>());
    require_same_result(a.get<char>(), b.get<char>());
    require_same_result(a.get<char8_t>(), b.get<char8_t>());
    require_same_result(a.get<char16_t>(), b.get<char16_t>());
    require_same_result(a.get<char32_t>(), b.get<char32_t>());
    require_same_result(a.get<wchar_t>(), b.get<wchar_t>());
    require_same_result(a.get<bool>(), b.get<bool>());
    require_same_result(a.get<std::nullptr_t>(), b.get<std::nullptr_t>());
    require_same_result(a.get<double>(), b.get<double>());
    require_same_result(a.get<cbor::simple_value>(), b.get<cbor::simple_value>());
    require_same_result(a.get<std::string_view>(), b.get<std::string_view>());
#ifdef __STDCPP_FLOAT128_T__
    auto const fa = a.get<std::float128_t>();
    auto const fb = b.get<std::float128_t>();
    require(fa.has_value() == fb.has_value());
#endif
}

// The forms of the input that cbor::lazy takes: owned by adopt, a std::shared_ptr to the bytes, and an
// owner with a view into it. Each form reads the same; operator[] is at; elements and entries give as many
// iterations as size says, and the post-increment steps as the pre-increment does.
inline void lazy_forms(std::string_view const input)
{
    auto const owned = cbor::lazy::adopt(std::string{input});
    auto const shared = cbor::lazy::view(std::make_shared<std::string const>(input));
    auto const bytes = std::make_shared<std::vector<char> const>(input.begin(), input.end());
    auto const borrowed = cbor::lazy::view(bytes, std::span<char const>(*bytes));
    require(owned.has_value() == shared.has_value() && owned.has_value() == borrowed.has_value());
    if (!owned) {
        require(owned.error() == shared.error() && owned.error() == borrowed.error());
        return;
    }
    node_reads_compare(*owned, *shared);
    node_reads_compare(*owned, *borrowed);
    std::string_view const key = input.substr(0, 4);
    require_same_result((*owned)[key].and_then([](cbor::lazy const &l) { return l.type(); }),
                        owned->at(key).and_then([](cbor::lazy const &l) { return l.type(); }));
    require_same_result((*owned)[cbor::key{0}].and_then([](cbor::lazy const &l) { return l.type(); }),
                        owned->at(cbor::key{0}).and_then([](cbor::lazy const &l) { return l.type(); }));
    require_same_result((*owned)[std::size_t{0}].and_then([](cbor::lazy const &l) { return l.type(); }),
                        owned->at(std::size_t{0}).and_then([](cbor::lazy const &l) { return l.type(); }));
    if (auto const elements = owned->elements()) {
        std::uint64_t count = 0;
        auto it = elements->begin();
        for (; it != elements->end() && count <= input.size(); it++) {
            if (!*it)
                break;
            node_reads_compare(**it, **it);
            ++count;
        }
        if (it == elements->end())
            require(count == elements->size());
    }
    if (auto const entries = owned->entries()) {
        std::uint64_t count = 0;
        auto it = entries->begin();
        for (; it != entries->end() && count <= input.size(); it++) {
            if (!*it)
                break;
            ++count;
        }
        if (it == entries->end())
            require(count == entries->size());
    }
}

inline void lazy_target(std::string_view const input)
{
    lazy_channels(input);
    lazy_forms(input);
}

} // namespace fuzz
