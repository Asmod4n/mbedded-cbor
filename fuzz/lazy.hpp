#pragma once

#include "common.hpp"

namespace fuzz
{

inline void lazy_channels(std::string_view const input)
{
    test_binding binding;
    auto const eager = cbor::decode<16>(binding, input);
    (void)cbor::doc_end<16>(input);
    auto const decoded = cbor::lazy::from(std::string{input});
    if (!decoded)
        return;
    cbor::lazy const &root = *decoded;
    auto const whole = cbor::lazy_decode<16>(binding, root);
    if (eager && whole)
        require(same(*eager, *whole));
    if (whole) {
        string_writer w;
        if (cbor::encode<16>(binding, w, *whole)) {
            auto const again = cbor::decode<16>(binding, w.bytes);
            require(again.has_value() && same_number(*whole, *again));
        }
    }
    if (eager) {
        if (auto const *a = test::get_if<test::array>(*eager)) {
            if (auto const elements = root.elements<16>()) {
                std::size_t i = 0;
                for (auto const e : *elements) {
                    if (!e || i >= a->size())
                        break;
                    if (auto const d = cbor::lazy_decode<16>(binding, *e))
                        require(same(*d, a->at(i)));
                    if (auto const at = root.at<16>(static_cast<std::int64_t>(i)))
                        if (auto const d = cbor::lazy_decode<16>(binding, *at))
                            require(same(*d, a->at(i)));
                    ++i;
                }
            }
        }
        if (auto const *m = test::get_if<test::map>(*eager)) {
            if (auto const entries = root.entries<16>()) {
                std::size_t i = 0;
                for (auto const e : *entries) {
                    if (!e || i >= m->size())
                        break;
                    if (auto const k = cbor::lazy_decode<16>(binding, e->first))
                        require(same(*k, m->at(i).key));
                    if (auto const v = cbor::lazy_decode<16>(binding, e->second))
                        require(same(*v, m->at(i).val));
                    ++i;
                }
            }
        }
    }
    std::string_view const key = input.substr(0, 4);
    (void)root.at<16>(std::int64_t{0});
    (void)root.at<16>(std::int64_t{-1});
    (void)root.at<16>("a");
    if (auto const a = root.at<16>(key))
        if (auto const b = a->at<16>(std::int64_t{0}))
            (void)b->at<16>(key);
}

inline void lazy_target(std::string_view const input)
{
    lazy_channels(input);
}

} // namespace fuzz
