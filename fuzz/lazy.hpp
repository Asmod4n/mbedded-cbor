#pragma once

#include "common.hpp"

namespace fuzz
{

inline void lazy_channels(std::string_view const input)
{
    test_host host;
    auto const eager = cbor::decode<16>(host, input);
    (void)cbor::doc_end<16>(input);
    auto const decoded = cbor::decode<16>(std::string{input});
    if (!decoded)
        return;
    cbor::lazy const &root = *decoded;
    auto const whole = cbor::lazy_decode<16>(host, root);
    if (eager && whole)
        require(same(*eager, *whole));
    if (whole) {
        string_writer w;
        if (cbor::encode<16>(host, w, *whole)) {
            auto const again = cbor::decode<16>(host, w.bytes);
            require(again.has_value() && same_number(*whole, *again));
        }
    }
    if (eager) {
        if (auto const *a = std::get_if<test::array>(&eager->kind)) {
            if (auto const elements = cbor::lazy_elements_of<16>(root)) {
                std::size_t i = 0;
                for (auto const e : *elements) {
                    if (!e || i >= a->size())
                        break;
                    if (auto const d = cbor::lazy_decode<16>(host, *e))
                        require(same(*d, a->at(i)));
                    if (auto const at = cbor::lazy_at<16>(root, static_cast<std::int64_t>(i)))
                        if (auto const d = cbor::lazy_decode<16>(host, *at))
                            require(same(*d, a->at(i)));
                    ++i;
                }
            }
        }
        if (auto const *m = std::get_if<test::map>(&eager->kind)) {
            if (auto const entries = cbor::lazy_entries_of<16>(root)) {
                std::size_t i = 0;
                for (auto const e : *entries) {
                    if (!e || i >= m->size())
                        break;
                    if (auto const k = cbor::lazy_decode<16>(host, e->first))
                        require(same(*k, m->at(i).key));
                    if (auto const v = cbor::lazy_decode<16>(host, e->second))
                        require(same(*v, m->at(i).val));
                    ++i;
                }
            }
        }
    }
    std::string_view const key = input.substr(0, 4);
    (void)cbor::lazy_at<16>(root, std::int64_t{0});
    (void)cbor::lazy_at<16>(root, std::int64_t{-1});
    (void)cbor::lazy_at<16>(root, "a");
    if (auto const a = cbor::lazy_at<16>(root, key))
        if (auto const b = cbor::lazy_at<16>(*a, std::int64_t{0}))
            (void)cbor::lazy_at<16>(*b, key);
}

inline void lazy_target(std::string_view const input)
{
    lazy_channels(input);
}

} // namespace fuzz
