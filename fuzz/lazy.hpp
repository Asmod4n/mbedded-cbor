#pragma once

#include "common.hpp"

namespace fuzz
{

inline void lazy_channels(std::string_view const input)
{
    test_binding binding;
    auto const eager = cbor::lazy_decode(binding, *cbor::lazy::from(input));
    (void)cbor::item_size(input);
    auto const decoded = cbor::lazy::from(std::string{input});
    if (!decoded)
        return;
    cbor::lazy const &root = *decoded;
    auto const whole = cbor::lazy_decode(binding, root);
    if (eager && whole)
        require(same(*eager, *whole));
    if (whole) {
        string_writer w;
        if (cbor::encode(binding, w, *whole)) {
            auto const again = cbor::lazy_decode(binding, *cbor::lazy::from(w.encoded));
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
                    if (auto const d = cbor::lazy_decode(binding, *e))
                        require(same(*d, a->at(i)));
                    if (auto const at = root.at(i))
                        if (auto const d = cbor::lazy_decode(binding, *at))
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
                    if (auto const k = cbor::lazy_decode(binding, e->first))
                        require(same(*k, m->at(i).key));
                    if (auto const v = cbor::lazy_decode(binding, e->second))
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

inline void lazy_target(std::string_view const input)
{
    lazy_channels(input);
}

} // namespace fuzz
